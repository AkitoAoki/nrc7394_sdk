//**************************************************************************
//  システム名 : WAH0070 / NRC7292・NRC7394 OV2640 カメラ制御サンプル
//  概要       : OV2640カメラモジュール制御用ヘッダーファイル
//             : SPI接続、I2C/SCCB接続、解像度設定、画像キャプチャ処理の
//             : 公開関数プロトタイプおよびハードウェア接続定義を行う。
//--------------------------------------------------------------------------
//  バージョン   日付        更新区分    内容
//  1.00.00      24/--/--    新規        Newracom社サンプルコード
//  1.00.01      26/05/11    変更        コメントガイドライン形式に合わせて
//                                      日本語コメントを追加
//**************************************************************************
//
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
//
//**************************************************************************

#ifndef __NRC_DEV_OV2640_H__
#define __NRC_DEV_OV2640_H__

#ifdef __cplusplus
extern "C" {
#endif

//======================================================
//  OV2640 I2C/SCCB デバイスアドレス定義
//======================================================
//  OV2640の制御レジスタへアクセスするためのI2C/SCCBアドレス。
//  本サンプルでは、Newracom SDKのI2C APIを使用してOV2640の
//  内部レジスタを設定する。
//
//  注意：
//  OV2640の一般的なSCCBアドレスは資料により 0x30 / 0x60 の
//  表記差がある。0x30は7bitアドレス、0x60はR/Wビットを含めた
//  8bitライトアドレス相当として扱われる場合がある。
//  本サンプルではSDK側のAPI仕様に合わせ、元コード通り0x60を使用する。
//======================================================
#define OV2640_ADDR 0x60

//======================================================
//  OV2640 SPI ピン定義
//======================================================
//  OV2640単体は画像データをDVPパラレルで出力するが、
//  このサンプルではArduCAM互換構成を想定し、
//  SPI経由でFIFOおよび制御レジスタへアクセスする。
//
//  NRC7394定義あり：NRC7394向けピン割り当て
//  NRC7394定義なし：その他Newracom評価環境向けピン割り当て
//
//  接続信号：
//    MISO : SPI Master In Slave Out
//    MOSI : SPI Master Out Slave In
//    CS   : SPI Chip Select
//    SCLK : SPI Clock
//======================================================
#ifdef NRC7394
#define OV2640_SPI_MISO 29
#define OV2640_SPI_MOSI 6
#define OV2640_SPI_CS   28
#define OV2640_SPI_SCLK 7
#else
#define OV2640_SPI_MISO 12
#define OV2640_SPI_MOSI 13
#define OV2640_SPI_CS   14
#define OV2640_SPI_SCLK 15
#endif

//======================================================
//  OV2640 SPI 通信条件定義
//======================================================
//  SPI_MODE0：クロック極性CPOL=0、クロック位相CPHA=0
//  SPI_BIT8 ：8bit単位で送受信
//  6MHz     ：SPIクロック周波数
//
//  注意：
//  配線長、レベル変換、FPGAブリッジ、カメラモジュールの品質により、
//  6MHzで通信が不安定になる場合がある。その場合はクロックを下げて
//  SPIテストレジスタの読み書きが安定するか確認する。
//======================================================
#define OV2640_SPI_MODE  SPI_MODE0
#define OV2640_SPI_BIT   SPI_BIT8
#define OV2640_SPI_CLOCK 6000000

//======================================================
//  OV2640 I2C/SCCB ピン定義
//======================================================
//  OV2640の内部レジスタ設定用通信ライン。
//  OV2640ではSCCBと呼ばれる2線式制御バスを使用するが、
//  実装上はI2C APIでアクセスしている。
//
//  NRC7394定義あり：NRC7394向けピン割り当て
//  NRC7394定義なし：その他Newracom評価環境向けピン割り当て
//======================================================
#ifdef NRC7394
#define OV2640_I2C_SCL          24
#define OV2640_I2C_SDA          18
#else
#define OV2640_I2C_SCL          16
#define OV2640_I2C_SDA          17
#endif

//======================================================
//  OV2640 I2C/SCCB 通信条件定義
//======================================================
//  OV2640_I2C_CLOCK        : I2C/SCCB通信クロック。標準100kHz設定。
//  OV2640_I2C_CLOCK_SOURCE : SDK側I2Cクロックソース指定。
//  OV2640_I2C_DELAY_MS     : レジスタアクセス後の待ち時間。
//
//  注意：
//  カメラ初期化時は連続して多数のレジスタを書き込むため、
//  通信が不安定な場合は OV2640_I2C_DELAY_MS を大きくすることで
//  安定する可能性がある。
//======================================================
#define OV2640_I2C_CLOCK        100000
#define OV2640_I2C_CLOCK_SOURCE 0
#define OV2640_I2C_DELAY_MS     1

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：OV2640カメラモジュールを初期化する
//--------------------------------------------------------------------------
//  引    数：none
//  戻 り 値：none
//--------------------------------------------------------------------------
//  詳細説明：
//      SPI制御部、I2C/SCCB制御部を初期化し、SPIテストレジスタの
//      読み書き確認、OV2640のID確認、JPEG出力用レジスタ設定を行う。
//      実体処理は nrc_dev_ov2640.c 側で実装する。
//**************************************************************************
void ov2640_init(void);

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：OV2640の撮影開始を指示し、撮影完了まで待機する
//--------------------------------------------------------------------------
//  引    数：none
//  戻 り 値：0以上=撮影画像サイズ[byte] / 0未満=異常
//--------------------------------------------------------------------------
//  詳細説明：
//      ArduCAM互換FIFOをクリア後、撮影開始ビットを設定し、
//      CAP_DONE状態になるまで待機する。
//      撮影完了後、FIFOサイズレジスタを読み出し、JPEG画像サイズを返す。
//**************************************************************************
int ov2640_capture_and_wait(void);

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：撮影済み画像データをFIFOから読み出す
//--------------------------------------------------------------------------
//  引    数：uint8_t *buf  : 画像データ格納先バッファ
//           uint32_t size : 読み出しサイズ[byte]
//  戻 り 値：none
//--------------------------------------------------------------------------
//  詳細説明：
//      ov2640_capture_and_wait()で取得した画像サイズを元に、
//      SPI経由でFIFO内のJPEGデータを読み出す。
//      呼び出し側は size 以上のバッファ領域を事前に確保しておくこと。
//**************************************************************************
void ov2640_read_captured_bytes(uint8_t *buf, uint32_t size);

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：OV2640のJPEG出力解像度を設定する
//--------------------------------------------------------------------------
//  引    数：int index : 解像度選択番号
//                    0=160x120
//                    1=176x144
//                    2=320x240
//                    3=352x288
//                    4=640x480
//                    5=800x600
//                    6=1024x768
//                    7=1280x1024
//                    8=1600x1200
//  戻 り 値：none
//--------------------------------------------------------------------------
//  詳細説明：
//      ov2640_regs.h 側に定義された解像度別レジスタテーブルを使用し、
//      I2C/SCCB経由でOV2640の出力解像度を変更する。
//      範囲外のindexが指定された場合、実装側でエラー表示して処理を中断する。
//**************************************************************************
void set_resolution(int index);

#ifdef __cplusplus
}
#endif

#endif
