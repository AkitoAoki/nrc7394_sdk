//**************************************************************************
//  MIT License
//
//  Copyright (c) 2024 Newracom, Inc.
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.
//**************************************************************************

//**************************************************************************
//  システム名 : WAH0070 / Newracom NRC SDK OV2640 ArduCAM サンプル
//  概要       : WAH0070系モジュールでOV2640カメラ画像を取得し、
//             : Wi-Fi接続後にTCP通信で画像データを送信するサンプル処理
//--------------------------------------------------------------------------
//  バージョン   日付        更新区分    内容
//  1.00.00      26/05/11    新規        Newracomサンプルへ日本語詳細コメント追加
//
//**************************************************************************

//======================================================
//  インクルードファイル
//======================================================

// Newracom SDK基本機能
// nrc_usr_print、nrc_mem_malloc、nrc_mem_free、_delay_msなどを使用する。
#include "nrc_sdk.h"

// lwIP OS抽象化、ソケット通信、エラー番号、名前解決関連
// 本ファイルではTCP接続処理そのものはnetwork_service側へ分離されているが、
// errno表示などのためにlwIP関連ヘッダを取り込んでいる。
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/errno.h"
#include "lwip/netdb.h"

// Wi-Fi設定値取得用ヘッダ
// SSID、パスワード、送信先IPアドレス、送信先ポートなどの設定に使用する。
#include "wifi_config_setup.h"

// Wi-Fi接続共通処理用ヘッダ
// start_wifi()など、アクセスポイント接続処理に使用する。
#include "wifi_connect_common.h"

// Wi-Fiタスク関連ヘッダ
// SDK側のWi-Fi制御タスク定義を参照する。
#include "wifi_task.h"

// OV2640カメラ制御ドライバ
// カメラ初期化、撮影、FIFOからのJPEGデータ読出しに使用する。
#include "nrc_dev_ov2640.h"

// TCP送信サービス
// open_connection()、upload_data_packet()などを使用する。
#include "network_service.h"

//======================================================
//  ファイル内グローバル変数
//======================================================

// Wi-Fiおよび送信先設定情報
// nrc_wifi_set_config()でSDK側へ設定され、
// start_wifi()および画像送信先情報として使用される。
//
// 主な想定メンバ：
//   remote_addr : 画像送信先IPアドレス文字列
//   remote_port : 画像送信先TCPポート番号
static WIFI_CONFIG wifi_config;

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：撮影済み画像データをTCP経由で送信する
//--------------------------------------------------------------------------
//  引    数：char     *remote_addr : 送信先IPアドレス文字列
//          ：uint32_t  port        : 送信先TCPポート番号
//          ：char     *buf         : 送信対象画像データ格納バッファ
//          ：int       len         : 送信対象画像データサイズ[byte]
//  戻 り 値：0=正常 / -1=送信異常
//**************************************************************************
static int send_image_data(char *remote_addr, uint32_t port, char *buf, int len)
{
	int ret;

	//======================================================
	//  画像サイズ表示
	//======================================================
	// 送信前にJPEG画像サイズをログへ出力する。
	// 実運用時は、このサイズを確認することで、
	// カメラ設定、解像度、JPEG圧縮状態の異常を追跡しやすくなる。
	nrc_usr_print("[%s]: image size : %u\n", __func__, len);

	//======================================================
	//  TCP送信処理
	//======================================================
	// upload_data_packet()はnetwork_service.c側の送信関数。
	// 内部でsend()失敗時の再接続・再送処理を持つ想定である。
	//
	// remote_addr : Wi-Fi設定から取得した送信先IP
	// port        : Wi-Fi設定から取得した送信先ポート
	// buf         : OV2640 FIFOから読み出したJPEG画像データ
	// len         : JPEG画像サイズ
	if (!upload_data_packet(remote_addr, port, (char *)buf, len)) {
		// 送信失敗時はerrnoを表示する。
		// lwIP環境ではerrno値から、接続断、タイムアウト、相手側リセットなどを切り分ける。
		nrc_usr_print("Failed to send(), errno=%d\n", errno);
		return -1;
	}

	//======================================================
	//  正常終了
	//======================================================
	return 0;
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：OV2640で1枚撮影し、取得したJPEG画像をTCP送信する
//--------------------------------------------------------------------------
//  引    数：none
//  戻 り 値：NRC_SUCCESS=正常 / NRC_FAIL=異常
//**************************************************************************
static int send_photo(void)
{
	char *buf = NULL;

	//======================================================
	//  撮影開始ログ
	//======================================================
	nrc_usr_print("[SEND PHOTO]\n");

	//======================================================
	//  OV2640撮影実行
	//======================================================
	// ov2640_capture_and_wait()は、
	//   1. ArduChip FIFOクリア
	//   2. キャプチャ開始
	//   3. 撮影完了フラグ待ち
	//   4. FIFOサイズ取得
	// を行い、撮影されたJPEG画像サイズを返す。
	int image_size = ov2640_capture_and_wait();
	nrc_usr_print("IMAGE SIZE = %d\n", image_size);

	//======================================================
	//  JPEG画像格納バッファ確保
	//======================================================
	// OV2640/ArduCAMのFIFOから画像を読み出すため、
	// 撮影サイズ分のRAMを動的確保する。
	//
	// 注意：
	//   1600x1200など高解像度ではJPEGサイズが大きくなり、
	//   メモリ不足になる可能性がある。
	//   低消費電力IoTカメラ用途では、解像度・画質・送信頻度を調整すること。
	buf = nrc_mem_malloc(image_size);
	if (!buf) {
		// メモリ確保に失敗した場合は撮影データを送信できないため異常終了する。
		nrc_usr_print("[%s] Error allocating buffer for image\n", __func__);
		return NRC_FAIL;
	}

	//======================================================
	//  OV2640 FIFOからJPEGデータ読出し
	//======================================================
	// ov2640_read_captured_bytes()は、ArduCAM互換FIFOから
	// SPIまたはSPI DMAを使用してJPEGバイナリを読み出す。
	//
	// buf        : 読出し先バッファ
	// image_size : 読出しバイト数
	ov2640_read_captured_bytes((uint8_t *)buf, image_size);

	//======================================================
	//  JPEG画像データ送信
	//======================================================
	// Wi-Fi設定に保持されている送信先IPアドレス・ポート番号へ送信する。
	// 本サンプルでは画像データ本体のみを送信しており、
	// ファイル名、サイズヘッダ、チェックサムなどの独自ヘッダは付与していない。
	send_image_data(wifi_config.remote_addr, wifi_config.remote_port, buf, image_size);

	//======================================================
	//  動的確保メモリ解放
	//======================================================
	// 送信完了後は必ずバッファを解放する。
	// 長時間連続撮影する場合、ここを忘れるとメモリリークになる。
	nrc_mem_free(buf);

	//======================================================
	//  正常終了
	//======================================================
	return NRC_SUCCESS;
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：指定周期で撮影とTCP送信を繰り返す
//--------------------------------------------------------------------------
//  引    数：int delay : 撮影送信間隔[秒]
//  戻 り 値：none
//**************************************************************************
static void stream_photo(int delay)
{
	//======================================================
	//  連続撮影・連続送信ループ
	//======================================================
	// send_photo()が正常終了している間、撮影と送信を繰り返す。
	// send_photo()がNRC_FAILを返した場合、ループを抜けて処理終了となる。
	while (send_photo() == NRC_SUCCESS) {
		//======================================================
		//  撮影間隔待ち
		//======================================================
		// delayが0より大きい場合のみ、指定秒数待機する。
		// 例：delay=3の場合、約3秒間隔で画像送信する。
		if (delay > 0) {
			_delay_ms(delay * 1000);
		}
	}
}

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：ユーザーアプリケーション初期化処理を実行する
//--------------------------------------------------------------------------
//  引    数：none
//  戻 り 値：none
//**************************************************************************
void user_init(void)
{
	int retry_delay = 10;

	//======================================================
	//  Wi-Fi設定初期化
	//======================================================
	// SDKへWi-Fi設定構造体を登録する。
	// wifi_configには、SSID、パスワード、送信先IP、送信先ポートなどが
	// 設定される想定である。
	nrc_wifi_set_config(&wifi_config);

	//======================================================
	//  UARTコンソール有効化
	//======================================================
	// nrc_usr_print()によるデバッグログをUARTへ出力できるようにする。
	nrc_uart_console_enable(true);

	//======================================================
	//  OV2640カメラ初期化
	//======================================================
	// SPI、I2C、ArduChipレジスタ、OV2640センサレジスタを初期化する。
	// カメラID確認やJPEGモード設定もこの中で行われる。
	ov2640_init();

	//======================================================
	//  Wi-Fi接続開始
	//======================================================
	// 設定済みSSIDへ接続し、ネットワーク通信可能な状態にする。
	start_wifi(&wifi_config);

	//======================================================
	//  TCP接続確立待ち
	//======================================================
	// 送信先サーバへTCP接続を試みる。
	// 接続できない場合は、retry_delay秒待って再試行する。
	//
	// 注意：
	//   ここは接続成功まで抜けないループである。
	//   実運用では、最大リトライ回数、ウォッチドッグ、低電力スリープ移行などを
	//   追加すると、太陽電池・EDLC駆動時に扱いやすくなる。
	while (!open_connection(wifi_config.remote_addr, wifi_config.remote_port)) {
		nrc_usr_print("** Error connecting to %s:%d, retry in %d sec.\n",
				wifi_config.remote_addr, wifi_config.remote_port, retry_delay);
		_delay_ms(retry_delay * 1000);
	}

	//======================================================
	//  画像ストリーミング開始
	//======================================================
	// 3秒間隔で撮影し、TCP接続先へJPEGデータを送信する。
	// 本サンプルでは終了条件はsend_photo()失敗時のみである。
	stream_photo(3);
}
