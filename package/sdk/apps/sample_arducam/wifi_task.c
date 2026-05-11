//**************************************************************************
//  システム名 : WAH0070 サンプルソフトウェア
//  概要       : Newracom NRC SDK を使用した Wi-Fi 初期化・接続制御処理
//--------------------------------------------------------------------------
//  バージョン   日付        更新区分    内容
//  1.00.00      24/--/--    新規        Newracom サンプルソース
//  1.00.01      26/05/11    変更        コメントガイドラインに沿った詳細コメントを追加
//
//**************************************************************************
//  ライセンス : MIT License
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

//======================================================
//  インクルードファイル
//======================================================
#include "nrc_sdk.h"                // NRC SDK 基本API、ログ出力、戻り値定義など
#include "wifi_config_setup.h"      // WIFI_CONFIG構造体、Wi-Fi設定読込関連定義
#include "wifi_connect_common.h"    // wifi_init(), wifi_connect() などの共通Wi-Fi接続API

//======================================================
//  ファイル内 static 変数
//======================================================
// Wi-Fi 仮想インターフェースID
//
// 【補足】
// 本ファイル内では現在参照されていないが、NRC SDK のWi-Fi制御では
// 仮想インターフェース番号を扱う構成があるため、将来拡張用または
// サンプルコード由来の変数として残されている可能性がある。
static int vif_id = 0;

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：Wi-Fi機能を初期化する
//--------------------------------------------------------------------------
//  引    数：WIFI_CONFIG* param : Wi-Fi接続設定情報へのポインタ
//            - SSID
//            - 認証方式
//            - パスワード
//            - IP設定
//            - リモート接続先情報などを含む
//
//  戻 り 値：NRC_SUCCESS = 正常
//            NRC_FAIL    = 異常
//
//  詳細説明：
//            wifi_init() を呼び出し、NRC Wi-Fiドライバおよび接続に必要な
//            基本設定を初期化する。
//            初期化に失敗した場合はログを出力して NRC_FAIL を返す。
//            初期化に成功した場合はログを出力して NRC_SUCCESS を返す。
//
//  注意事項：
//            本関数は Wi-Fi接続前に必ず呼び出す必要がある。
//            param が NULL の場合の保護処理は元サンプルには無いため、
//            呼び出し元で有効なポインタを渡す前提である。
//**************************************************************************
static int run_wifi_init(WIFI_CONFIG* param)
{
	//======================================================
	//  Wi-Fi初期化処理
	//======================================================
	// wifi_init() にWi-Fi設定情報を渡し、NRC SDK側のWi-Fi機能を初期化する。
	// 戻り値が WIFI_SUCCESS 以外の場合は初期化失敗と判断する。
	if (wifi_init(param) != WIFI_SUCCESS) {
		// 初期化失敗ログを出力する。
		// __func__ は現在実行中の関数名へ展開される。
		nrc_usr_print("[%s] Fail\n", __func__);

		// NRC SDK系の異常戻り値として NRC_FAIL を返す。
		return NRC_FAIL;
	}

	//======================================================
	//  Wi-Fi初期化成功
	//======================================================
	// Wi-Fi初期化が正常に完了したことをログへ出力する。
	nrc_usr_print("[%s] Success\n", __func__);

	// 正常終了を返す。
	return NRC_SUCCESS;
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：Wi-Fiアクセスポイントへ接続する
//--------------------------------------------------------------------------
//  引    数：WIFI_CONFIG* param : Wi-Fi接続設定情報へのポインタ
//            - 接続先SSID
//            - 認証情報
//            - DHCP/固定IPなどのネットワーク設定を含む
//
//  戻 り 値：NRC_SUCCESS = 正常
//            NRC_FAIL    = 本実装では通常返らない
//
//  詳細説明：
//            wifi_connect() を使用してアクセスポイントへ接続する。
//            接続に成功するまで while(1) で繰り返し接続を試行する。
//            接続成功時は NRC_SUCCESS を返す。
//
//  注意事項：
//            本関数は接続成功まで永久リトライする仕様である。
//            SSID、パスワード、認証方式、電波状態、AP動作状態などに問題がある場合、
//            この関数から戻らない可能性がある。
//            製品用途では、最大リトライ回数、タイムアウト、再起動処理などを
//            追加することが望ましい。
//**************************************************************************
static int run_wifi_connect(WIFI_CONFIG* param)
{
	//======================================================
	//  Wi-Fi接続リトライループ
	//======================================================
	// 接続に成功するまでループを継続する。
	while (1)
	{
		//======================================================
		//  アクセスポイント接続処理
		//======================================================
		// wifi_connect() にWi-Fi設定情報を渡し、指定SSIDへ接続する。
		if (wifi_connect(param) == WIFI_SUCCESS) {
			// 接続成功時は接続先SSIDをログに出力する。
			nrc_usr_print ("[%s] Success (%s) !! \n", __func__, param->ssid);

			// Wi-Fi接続成功として呼び出し元へ戻る。
			return NRC_SUCCESS;
		}
		else {
			//======================================================
			//  接続失敗時処理
			//======================================================
			// 接続に失敗した場合はログを出力し、ループ先頭へ戻って再接続する。
			// 本サンプルではリトライ待ち時間を明示的に入れていないため、
			// wifi_connect() 内部の待ち時間仕様に依存する。
			nrc_usr_print ("[%s] Fail (%s) - Retrying Connection\n", __func__, param->ssid);
		}
	}
}

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：Wi-Fi初期化およびアクセスポイント接続を開始する
//--------------------------------------------------------------------------
//  引    数：WIFI_CONFIG* wifi_config : Wi-Fi接続設定情報へのポインタ
//            - nrc_wifi_set_config() 等で設定されたWi-Fiパラメータを使用する
//
//  戻 り 値：none
//
//  詳細説明：
//            WAH0070サンプルアプリケーションの起動シーケンスから呼び出される
//            Wi-Fi接続開始用の公開関数である。
//            内部で以下の順に処理を実行する。
//
//            1. run_wifi_init()    : Wi-Fi機能の初期化
//            2. run_wifi_connect() : アクセスポイント接続
//
//            sample_arducam.c 側では、本関数でWi-Fi接続を完了した後に
//            TCPサーバーへ接続し、OV2640で撮影したJPEG画像を送信する。
//
//  注意事項：
//            run_wifi_init() の戻り値を本関数内では確認していない。
//            元サンプルの動作を維持しているが、製品用途では初期化失敗時に
//            接続処理へ進まないように戻り値判定を追加することが望ましい。
//**************************************************************************
void start_wifi(WIFI_CONFIG *wifi_config)
{
	//======================================================
	//  Wi-Fi初期化
	//======================================================
	// NRC SDKのWi-Fi機能を初期化する。
	// 本サンプルでは戻り値を参照していないため、初期化失敗時も次の接続処理へ進む。
	run_wifi_init(wifi_config);

	//======================================================
	//  Wi-Fi接続
	//======================================================
	// 指定SSIDへ接続する。
	// 接続成功まで内部でリトライを継続する。
	run_wifi_connect(wifi_config);
}
