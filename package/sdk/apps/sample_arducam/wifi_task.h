//**************************************************************************
//  システム名 : WAH0070 サンプルソフトウェア
//  概要       : Wi-Fi接続タスク 公開インターフェース定義
//--------------------------------------------------------------------------
//  バージョン   日付        更新区分    内容
//  1.00.00      26/05/11    新規        初版リリース
//
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

#ifndef __WIFI_TASK_H__
#define __WIFI_TASK_H__

#ifdef __cplusplus
extern "C" {
#endif

//======================================================
//  インクルードファイル
//======================================================
//  WIFI_CONFIG 型を使用するため、Wi-Fi設定定義ヘッダーを参照する。
//  呼び出し元がすでに include している場合でも、本ヘッダー単体で
//  型解決できるように明示的に include する。
#include "wifi_config_setup.h"

//======================================================
//  外部公開関数プロトタイプ宣言
//======================================================

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：Wi-Fi機能を初期化し、指定されたアクセスポイントへ接続する
//--------------------------------------------------------------------------
//  引    数：WIFI_CONFIG *wifi_config : Wi-Fi接続設定情報ポインタ
//           :                         : SSID、パスワード、認証方式、
//           :                         : リモート接続先情報などを保持する
//  戻 り 値：none
//--------------------------------------------------------------------------
//  補足説明：
//      本関数は wifi_task.c 側で実装される Wi-Fi起動用の公開APIである。
//      内部では Wi-Fi初期化処理 run_wifi_init() と、
//      アクセスポイント接続処理 run_wifi_connect() を順に実行する。
//
//      run_wifi_connect() は接続に成功するまでリトライする実装であるため、
//      アクセスポイント未起動、SSID誤り、パスワード誤り、電波未到達などの
//      状態では、本関数から復帰しない可能性がある。
//
//      WAH0070 の ArduCAM サンプルでは、user_init() から本関数を呼び出し、
//      Wi-Fi接続完了後に TCPサーバーへ接続し、OV2640で撮影したJPEG画像を
//      送信する流れになる。
//**************************************************************************
void start_wifi(WIFI_CONFIG *wifi_config);

#ifdef __cplusplus
}
#endif

#endif  // __WIFI_TASK_H__
