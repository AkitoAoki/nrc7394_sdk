//**************************************************************************
//  システム名 : WAH0070 / Newracom NRC SDK OV2640カメラ制御サンプル
//  概要       : OV2640カメラモジュールをSPI/I2C経由で初期化し、JPEG画像を
//             : キャプチャしてFIFOから読み出すためのサンプル実装
//--------------------------------------------------------------------------
//  バージョン   日付        更新区分    内容
//  1.00.00      24/--/--    新規        Newracomサンプル初版
//  1.00.01      26/05/11    変更        コメントガイドライン形式の日本語コメントを追加
//
//**************************************************************************
//  ライセンス : MIT License
//  Copyright  : Copyright (c) 2024 Newracom, Inc.
//--------------------------------------------------------------------------
//  本ファイルはNewracom社サンプルソフトを元に、処理内容を理解しやすくする
//  ための日本語コメントを追加したものです。
//
//  MITライセンス条文の要旨:
//  ・本ソフトウェアは無償で使用、複製、変更、配布、再許諾、販売可能です。
//  ・著作権表示および許諾表示を、すべての複製または重要部分に含める必要があります。
//  ・本ソフトウェアは無保証で提供されます。
//**************************************************************************

#include "nrc_sdk.h"
#include "ov2640_regs.h"
#include "nrc_dev_ov2640.h"
#include "api_dma.h"
#include "api_spi_dma.h"

//======================================================
//  定数定義
//======================================================

// OV2640のSPIアクセスでDMAを使用するかを指定する
// 1: SPI DMA使用
// 0: 通常SPI API使用
#define OV2640_USE_DMA 1

// ArduCAM互換FIFOのバースト読出し時に使用するチャンクサイズ
// 2048バイトの実データ + 1バイトのダミーデータを想定する
#define CHUNK_SIZE (2048 + 1)

//======================================================
//  グローバル変数
//======================================================

// OV2640 / ArduCAM互換SPIインターフェース制御用デバイス情報
// 主に画像FIFO、ArduChipレジスタ、キャプチャ制御レジスタへアクセスする
spi_device_t ov2640_spi;

// OV2640センサ制御用I2Cインターフェース制御情報
// 主にOV2640本体のセンサレジスタ設定、ID読出し、JPEG設定に使用する
i2c_device_t ov2640_i2c;

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：OV2640用SPIインターフェースを初期化する
//--------------------------------------------------------------------------
//  引    数：none
//  戻 り 値：none
//**************************************************************************
static void ov2640_init_spi()
{
	// SPIのMISOピンを設定する
	ov2640_spi.pin_miso = OV2640_SPI_MISO;

	// SPIのMOSIピンを設定する
	ov2640_spi.pin_mosi = OV2640_SPI_MOSI;

	// SPIのチップセレクトピンを設定する
	ov2640_spi.pin_cs   = OV2640_SPI_CS;

	// SPIのクロックピンを設定する
	ov2640_spi.pin_sclk = OV2640_SPI_SCLK;

	// 1フレームあたりの転送ビット数を設定する
	ov2640_spi.frame_bits = OV2640_SPI_BIT;

	// SPIクロック周波数を設定する
	ov2640_spi.clock = OV2640_SPI_CLOCK;

	// SPIモードを設定する
	ov2640_spi.mode = OV2640_SPI_MODE;

	// 使用するSPIコントローラを設定する
	ov2640_spi.controller = SPI_CONTROLLER_SPI0;

	// 割込み退避フラグを初期化する
	ov2640_spi.irq_save_flag = 0;

	// SPI割込みハンドラは使用しないためNULLを設定する
	ov2640_spi.isr_handler = NULL;

#if OV2640_USE_DMA
	// DMAを使用する場合はSPI DMAを初期化する
	spi_dma_init(&ov2640_spi);
#else
	// DMAを使用しない場合は通常のSPI Masterとして初期化する
	nrc_spi_master_init(&ov2640_spi);

	// SPIを有効化する
	nrc_spi_enable(&ov2640_spi, true);

	// デバイス安定待ち
	_delay_ms(100);
#endif
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：SPI経由でArduChip互換レジスタを1バイト読み出す
//--------------------------------------------------------------------------
//  引    数：uint8_t reg    : 読み出し対象レジスタアドレス
//            uint8_t *value : 読み出し値格納先ポインタ
//  戻 り 値：none
//**************************************************************************
static void ov2640_read_spi(uint8_t reg, uint8_t *value)
{
#if OV2640_USE_DMA
	// DMA読出しでは、レジスタ指定後にダミーバイトを含む2バイト読出しを行う
	uint8_t buf[2] = {0, 0};

	// 1バイト目はダミー相当、2バイト目が実データとなる想定で読み出す
	spi_dma_read(&reg, buf, 2);

	// 実データ側の1バイトを呼び出し元へ返す
	*value = buf[1];
#else
	// 通常SPI APIで1バイトレジスタを読み出す
	nrc_spi_readbyte_value(&ov2640_spi, reg, value);
#endif
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：SPI経由でArduChip互換レジスタへ1バイト書き込む
//--------------------------------------------------------------------------
//  引    数：uint8_t reg   : 書き込み対象レジスタアドレス
//            uint8_t value : 書き込み値
//  戻 り 値：none
//**************************************************************************
static void ov2640_write_spi(uint8_t reg, uint8_t value)
{
#if OV2640_USE_DMA
	// ArduChip互換SPIでは、書き込み時にレジスタアドレスのbit7を1にする
	uint8_t spi_buf[2] = {reg | 0x80, value};

	// レジスタアドレスとデータを連続送信する
	spi_dma_write(spi_buf, 2);
#else
	// 通常SPI API使用時も、書き込み指定としてbit7を1にする
	reg |= 0x80;

	// レジスタへ1バイト書き込む
	nrc_spi_writebyte_value(&ov2640_spi, reg, value);
#endif
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：OV2640用I2Cインターフェースを初期化する
//--------------------------------------------------------------------------
//  引    数：none
//  戻 り 値：none
//**************************************************************************
static void ov2640_init_i2c()
{
	// I2C SDAピンを設定する
	ov2640_i2c.pin_sda      = OV2640_I2C_SDA;

	// I2C SCLピンを設定する
	ov2640_i2c.pin_scl      = OV2640_I2C_SCL;

	// I2Cクロックソースを設定する
	ov2640_i2c.clock_source = OV2640_I2C_CLOCK_SOURCE;

	// 使用するI2C Masterコントローラを設定する
	ov2640_i2c.controller   = I2C_MASTER_0;

	// I2Cクロック周波数を設定する
	ov2640_i2c.clock        = OV2640_I2C_CLOCK;

	// OV2640のレジスタ幅は8bitとして扱う
	ov2640_i2c.width        = I2C_8BIT;

	// OV2640のI2Cスレーブアドレスを設定する
	ov2640_i2c.address      = OV2640_ADDR;

	// I2Cを初期化する
	nrc_i2c_init(&ov2640_i2c);

	// I2Cを有効化する
	nrc_i2c_enable(&ov2640_i2c, true);
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：I2C経由でOV2640レジスタを1バイト読み出す
//--------------------------------------------------------------------------
//  引    数：uint8_t sad    : I2Cスレーブアドレス
//            uint8_t reg    : 読み出し対象レジスタアドレス
//            uint8_t *value : 読み出し値格納先ポインタ
//  戻 り 値：none
//**************************************************************************
static void ov2640_read_i2c(uint8_t sad, uint8_t reg, uint8_t *value)
{
	// 読み出し対象レジスタアドレスを指定するため、まずWrite方向でアクセスする
	nrc_i2c_start(&ov2640_i2c);
	nrc_i2c_writebyte(&ov2640_i2c, sad);
	nrc_i2c_writebyte(&ov2640_i2c, reg);
	nrc_i2c_stop(&ov2640_i2c);
	_delay_ms(OV2640_I2C_DELAY_MS);

	// 指定済みレジスタから1バイト読み出すため、Read方向で再スタートする
	nrc_i2c_start(&ov2640_i2c);
	nrc_i2c_writebyte(&ov2640_i2c, sad | 0x1);
	nrc_i2c_readbyte (&ov2640_i2c, value, false);
	nrc_i2c_stop(&ov2640_i2c);
	_delay_ms(OV2640_I2C_DELAY_MS);
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：I2C経由でOV2640レジスタへ1バイト書き込む
//--------------------------------------------------------------------------
//  引    数：uint8_t sad   : I2Cスレーブアドレス
//            uint8_t reg   : 書き込み対象レジスタアドレス
//            uint8_t value : 書き込み値
//  戻 り 値：none
//**************************************************************************
static void ov2640_write_i2c(uint8_t sad, uint8_t reg, uint8_t value)
{
	// I2C書き込み開始
	nrc_i2c_start(&ov2640_i2c);

	// スレーブアドレスを送信する
	nrc_i2c_writebyte(&ov2640_i2c, sad);

	// 書き込み対象レジスタアドレスを送信する
	nrc_i2c_writebyte(&ov2640_i2c, reg);

	// レジスタへ書き込む値を送信する
	nrc_i2c_writebyte(&ov2640_i2c, value);

	// I2C書き込み終了
	nrc_i2c_stop(&ov2640_i2c);

	// OV2640側のレジスタ反映待ち
	_delay_ms(OV2640_I2C_DELAY_MS);
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：OV2640レジスタ設定テーブルを連続書き込みする
//--------------------------------------------------------------------------
//  引    数：uint8_t sad                       : I2Cスレーブアドレス
//            const struct sensor_reg *reg_vals : レジスタ設定テーブル
//  戻 り 値：none
//**************************************************************************
static void ov2640_write_i2c_multi(uint8_t sad, const struct sensor_reg *reg_vals)
{
	struct sensor_reg reg_val;

	// 未使用変数の警告対策として、引数sadを明示的に参照する
	// 元サンプルでは内部でOV2640_ADDR固定を使用している
	(void)sad;

	while(1)
	{
		// 設定テーブルから1組分のレジスタアドレスと設定値を取得する
		reg_val = *reg_vals++;

		// 0xFF, 0xFFをテーブル終端マーカーとして扱う
		if(reg_val.reg == 0xFF && reg_val.val == 0xFF)
		{
			return;
		}

		// OV2640へ1レジスタ分の設定を書き込む
		ov2640_write_i2c(OV2640_ADDR, reg_val.reg, reg_val.val);
	}
}

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：OV2640のJPEG解像度を設定する
//--------------------------------------------------------------------------
//  引    数：int index : 解像度選択番号
//            0=160x120
//            1=176x144
//            2=320x240
//            3=352x288
//            4=640x480
//            5=800x600
//            6=1024x768
//            7=1280x1024
//            8=1600x1200
//  戻 り 値：none
//**************************************************************************
void set_resolution(int index)
{
	// 注意:
	// 元サンプルでは、ここでデバッグ出力を有効にするとJPEG画像が壊れる可能性がある
	// 旨のコメントがある。カメラ転送タイミングへ影響する可能性があるため、
	// キャプチャ処理中の過剰ログ出力は避ける。
	// nrc_usr_print("[SET RESOLUTION: %d]\n", index);

	// 解像度テーブルの範囲外指定をチェックする
	if(index < 0 || 8 < index)
	{
		nrc_usr_print("[ERROR: INVALID RESOLUTION]\n");
		return;
	}

	// 選択された解像度をログ出力する
	switch(index)
	{
		case 0: nrc_usr_print("160x120_JPEG\n");   break;
		case 1: nrc_usr_print("176x144_JPEG\n");   break;
		case 2: nrc_usr_print("320x240_JPEG\n");   break;
		case 3: nrc_usr_print("352x288_JPEG\n");   break;
		case 4: nrc_usr_print("640x480_JPEG\n");   break;
		case 5: nrc_usr_print("800x600_JPEG\n");   break;
		case 6: nrc_usr_print("1024x768_JPEG\n");  break;
		case 7: nrc_usr_print("1280x1024_JPEG\n"); break;
		case 8: nrc_usr_print("1600x1200_JPEG\n"); break;
	}

	// 解像度別のレジスタ設定テーブルをOV2640へ書き込む
	ov2640_write_i2c_multi(OV2640_ADDR, resolutions[index]);
}

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：OV2640カメラとArduChip互換SPI制御部を初期化する
//--------------------------------------------------------------------------
//  引    数：none
//  戻 り 値：none
//**************************************************************************
void ov2640_init()
{
	nrc_usr_print("<OV2640 INIT>\n");

	//======================================================
	//  SPI / I2C 初期化
	//======================================================
	nrc_usr_print("[SPI/I2C INIT]\n");
	ov2640_init_spi();
	ov2640_init_i2c();

	//======================================================
	//  SPI通信確認
	//======================================================
	nrc_usr_print("[SPI TEST]\n");
	while(1)
	{
		// ArduChipテストレジスタへ既知値を書き込み、同じ値が読めるか確認する
		uint8_t wspi_test_value = 0x55, rspi_test_value;
		ov2640_write_spi(ARDUCHIP_TEST1, wspi_test_value);
		ov2640_read_spi(ARDUCHIP_TEST1, &rspi_test_value);

		// 書き込み値と読み出し値が一致すればSPI通信正常と判断する
		if(wspi_test_value == rspi_test_value)
		{
			break;
		}

		// SPI通信が成立していない場合は1秒待って再確認する
		_delay_ms(1000);
	}
	nrc_usr_print("=> SUCCESS\n");

	//======================================================
	//  MCUモード設定
	//======================================================
	nrc_usr_print("[MCU MODE SELECTION]\n");

	// ArduChip互換制御部をMCU制御モードへ設定する
	ov2640_write_spi(ARDUCHIP_MODE, 0x00);

	//======================================================
	//  I2C ID確認
	//======================================================
	nrc_usr_print("[I2C IDENTIFICATION]\n");

	// OV2640のセンサバンクを選択する
	ov2640_write_i2c(OV2640_ADDR, 0xff, 0x01);

	// OV2640のチップID上位/下位を読み出す
	uint8_t id_msb, id_lsb;
	ov2640_read_i2c(OV2640_ADDR, 0x0A, &id_msb);
	ov2640_read_i2c(OV2640_ADDR, 0x0B, &id_lsb);

	// IDを16bit値に結合する
	uint16_t id = id_msb << 8 | id_lsb;
	nrc_usr_print("ID(I2C)=0x%x\n", id);

	// OV2640の期待IDと一致するか確認する
	if(id != 0x2642)
	{
		nrc_usr_print("Error: ID mismatch (expected: 0x2642)\n");
		return;
	}
	nrc_usr_print("=> SUCCESS\n");

	//======================================================
	//  カメラ初期化
	//======================================================
	nrc_usr_print("[CAMERA INITIALIZATION]\n");

	// センサバンクを選択する
	ov2640_write_i2c(OV2640_ADDR, 0xff, 0x01);

	// OV2640をソフトリセットする
	ov2640_write_i2c(OV2640_ADDR, 0x12, 0x80);
	_delay_ms(100);

	// JPEG出力用の基本初期化テーブルを書き込む
	ov2640_write_i2c_multi(OV2640_ADDR, OV2640_JPEG_INIT);

	// YUV422出力関連の設定を書き込む
	ov2640_write_i2c_multi(OV2640_ADDR, OV2640_YUV422);

	// JPEG出力設定を書き込む
	ov2640_write_i2c_multi(OV2640_ADDR, OV2640_JPEG);

	// センサバンクを選択する
	ov2640_write_i2c(OV2640_ADDR, 0xff, 0x01);

	// 出力極性/同期関連と思われる設定を行う
	ov2640_write_i2c(OV2640_ADDR, 0x15, 0x00);

	// 解像度設定例
	// 必要に応じて使用する解像度のテーブルへ切り替える
	// ov2640_write_i2c_multi(OV2640_ADDR, OV2640_320x240_JPEG);
	// ov2640_write_i2c_multi(OV2640_ADDR, OV2640_640x480_JPEG);
	// ov2640_write_i2c_multi(OV2640_ADDR, OV2640_800x600_JPEG);

	// 初期状態では1600x1200 JPEGを設定する
	ov2640_write_i2c_multi(OV2640_ADDR, OV2640_1600x1200_JPEG);

	// 設定反映待ち
	_delay_ms(1000);
}

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：OV2640の画像キャプチャを開始し、完了まで待機する
//--------------------------------------------------------------------------
//  引    数：none
//  戻 り 値：0以上=キャプチャ画像サイズ / 0未満=異常
//**************************************************************************
int ov2640_capture_and_wait()
{
	// ArduChip FIFOをクリアする
	ov2640_write_spi(ARDUCHIP_FIFO, FIFO_CLEAR_MASK);

	// キャプチャ開始を指示する
	ov2640_write_spi(ARDUCHIP_FIFO, FIFO_START_MASK);

	// キャプチャ完了ビットを監視する
	uint8_t trigger_value;
	do
	{
		ov2640_read_spi(ARDUCHIP_TRIG, &trigger_value);
	}
	while(!(trigger_value & CAP_DONE_MASK));

	// FIFOサイズレジスタを3バイト読み出す
	uint8_t fifo_size1, fifo_size2, fifo_size3;
	ov2640_read_spi(0x42, &fifo_size1);
	ov2640_read_spi(0x43, &fifo_size2);
	ov2640_read_spi(0x44, &fifo_size3);

	// 3バイトのFIFOサイズを結合し、有効ビットのみ取り出して返す
	return (fifo_size1 | (fifo_size2 << 8) | (fifo_size3 << 16)) & 0x07fffff;
}

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：キャプチャ済みJPEGデータをFIFOから読み出す
//--------------------------------------------------------------------------
//  引    数：uint8_t *buf  : 読み出しデータ格納先バッファ
//            uint32_t size : 読み出しサイズ
//  戻 り 値：none
//**************************************************************************
void ov2640_read_captured_bytes(uint8_t *buf, uint32_t size)
{
#if OV2640_USE_DMA
	// DMA読出し時は、FIFOバーストリードの先頭にダミーバイトが入るため、
	// 2048バイト実データ単位で分割して読み出す
	int iter = size / (CHUNK_SIZE - 1);
	int remainder = size % (CHUNK_SIZE - 1);

	// FIFOバーストリードコマンド
	uint8_t addr = BURST_FIFO_READ;

	// 1回分のDMA読出し作業バッファ
	uint8_t chunk[CHUNK_SIZE];
	int i = 0;

	// 2048バイト単位で読み出せる分を処理する
	if (iter > 0) {
		for (i = 0; i < iter; i++) {
			// ダミーバイト1バイト + 実データ2048バイトを読み出す
			spi_dma_read(&addr, chunk, CHUNK_SIZE);

			// 先頭ダミーバイトを除外して、出力バッファへコピーする
			memcpy(buf + (i * (CHUNK_SIZE - 1)), chunk + 1, CHUNK_SIZE - 1);
		}
	}

	// 端数データがある場合は残り分を読み出す
	if (remainder) {
		// 注意:
		// 元サンプルではremainderバイトのみ読み出しているが、
		// ダミーバイトを除外する設計の場合は、環境によっては
		// remainder + 1バイト読出しが必要になる可能性がある。
		// 実機でJPEG末尾欠けが出る場合は、この点を確認する。
		spi_dma_read(&addr, chunk, remainder);

		// 先頭ダミーバイトを除外して、端数分を出力バッファへコピーする
		memcpy(buf + (i * (CHUNK_SIZE - 1)), chunk + 1, remainder);
	}
#else
	// DMAを使用しない場合は、FIFOから1バイトずつ読み出す
	for(int i = 0; i < size; i++)
	{
		ov2640_read_spi(SINGLE_FIFO_READ, buf++);
	}
#endif
}
