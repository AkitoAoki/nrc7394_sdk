//**************************************************************************
//  システム名 : WAH0070 / Newracom NRC TCP通信サンプル
//  概要       : WAH0070系SDK環境でTCPソケットを生成し、指定サーバへ接続、
//             : データ送信、受信、再接続リトライを行うサンプル処理。
//--------------------------------------------------------------------------
//  バージョン   日付        更新区分    内容
//  1.00.00      26/05/11    新規        既存サンプルへコメントガイドライン形式の
//                                      日本語コメントを追加
//**************************************************************************

//==========================================================================
//  ライセンス
//==========================================================================
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

//==========================================================================
//  インクルードファイル
//==========================================================================
#include "nrc_sdk.h"        // Newracom NRC SDK基本定義、ログ出力APIなど
#include "lwip/sys.h"       // lwIPシステム関連定義
#include "lwip/sockets.h"   // lwIPソケットAPI定義
#include "lwip/errno.h"     // lwIPエラー番号定義

//==========================================================================
//  ファイル内グローバル変数
//==========================================================================
static int sockfd = -1;     // TCPソケットディスクリプタ。未接続時は -1 とする。

//==========================================================================
//  定数定義
//==========================================================================
#define SOCK_TIMEOUT_SECONDS    1           // ソケットタイムアウト秒数。現状コード内では未使用。
#define MAX_RECV_LEN            (4 * 1024)  // 最大受信バッファサイズ想定値。現状コード内では未使用。
#define MAX_RETRY               5           // 接続・送信異常時の最大リトライ回数
#define MAX_SEND_RETRY          2           // send()失敗時の同一接続内リトライ回数

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：TCPサーバへ接続する
//--------------------------------------------------------------------------
//  引    数：char     *remote_address : 接続先IPアドレス文字列
//           uint16_t  port           : 接続先TCPポート番号
//  戻 り 値：true=正常 / false=異常
//**************************************************************************
bool open_connection(char *remote_address, uint16_t port)
{
#ifdef CONFIG_IPV6
    // IPv6有効時は、IPv6用アドレス構造体を使用する。
    struct sockaddr_in6 dest_addr;
#else
    // IPv4のみ使用時は、IPv4用アドレス構造体を使用する。
    struct sockaddr_in dest_addr;
#endif

    // IPv4処理側で共通的に参照するため、IPv4構造体ポインタへキャストする。
    struct sockaddr_in *addr_in = (struct sockaddr_in *)&dest_addr;

    // lwIP内部形式のIPアドレス格納領域。
    ip_addr_t remote_addr;

    // socket(), connect()などの戻り値確認用。
    int ret = 0;

    //======================================================================
    //  接続先IPアドレス文字列の解析
    //======================================================================
    //  ipaddr_aton()で文字列IPアドレスをlwIP内部形式へ変換する。
    //  変換できない場合は、IPアドレス指定ミス、またはIPv6無効環境で
    //  IPv6アドレスを指定した可能性がある。
    if (ipaddr_aton((char *)remote_address, &remote_addr)) {

        //==================================================================
        //  IPv4アドレス指定時の接続先情報設定
        //==================================================================
        if (IP_IS_V4(&remote_addr)) {
            nrc_usr_print("[%s] IPv4...\n", __func__);

            // IPv4ソケットアドレスとして、アドレスファミリ、ポート、IPを設定する。
            // sin_lenはlwIP/BSD系実装で使用される構造体長フィールド。
            addr_in->sin_family = AF_INET;
            addr_in->sin_len = sizeof(struct sockaddr_in);
            addr_in->sin_port = htons(port);
            addr_in->sin_addr.s_addr = inet_addr(remote_address);
        }
        else {
#ifdef CONFIG_IPV6
            //==============================================================
            //  IPv6アドレス指定時の接続先情報設定
            //==============================================================
            nrc_usr_print("[%s] IPv6...\n", __func__);

            // IPv6用ソケットアドレスとして、アドレスファミリ、ポート、
            // スコープID、IPv6アドレス本体を設定する。
            dest_addr.sin6_family = AF_INET6;
            dest_addr.sin6_len = sizeof(struct sockaddr_in6);
            dest_addr.sin6_port = htons(port);
            dest_addr.sin6_flowinfo = 0;
            dest_addr.sin6_scope_id = ip6_addr_zone(ip_2_ip6(&remote_addr));
            inet6_addr_from_ip6addr(&dest_addr.sin6_addr, ip_2_ip6(&remote_addr));
#else
            //==============================================================
            //  IPv6無効ビルドでIPv6アドレスが指定された場合
            //==============================================================
            nrc_usr_print("[%s] Unknown address type %s\n", __func__, remote_address);
            nrc_usr_print("[%s] Enable IPv6 to handle IPv6 remote address...\n", __func__);
            return false;
#endif
        }
    }
    else {
        //==================================================================
        //  IPアドレス文字列が不正な場合
        //==================================================================
        nrc_usr_print("[%s] Address %s not valid or enable IPv6 support.\n", __func__, remote_address);
        nrc_usr_print("[%s] use nvs set remote_address <ip address>\n", __func__);
        return false;
    }

    //======================================================================
    //  TCPソケット生成
    //======================================================================
    nrc_usr_print("[%s] Connecting to %s port %d...\n", __func__, remote_address, port);

    // SOCK_STREAMを指定してTCPソケットを生成する。
    sockfd = socket(addr_in->sin_family, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) {
        nrc_usr_print("Create socket failed\n");
        return false;
    }

    //======================================================================
    //  TCPサーバへ接続
    //======================================================================
    ret = connect(sockfd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (ret < 0) {
        // 接続失敗時は、ソケットを必ずcloseして未接続状態へ戻す。
        nrc_usr_print("open_connection failed\n");
        close(sockfd);
        sockfd = -1;
        return false;
    }

    return true;
}

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：TCP接続を切断し、ソケットを解放する
//--------------------------------------------------------------------------
//  引    数：none
//  戻 り 値：none
//**************************************************************************
void close_connection(void)
{
    // ソケットが有効な場合のみ切断処理を行う。
    if (sockfd >= 0) {
        nrc_usr_print("close_connection :%d\n", sockfd);

        // 送受信の両方向を停止してから、ソケットをcloseする。
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);

        // close後は、二重close防止のため未接続状態へ戻す。
        sockfd = -1;
    }
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：TCP接続を一度閉じてから再接続する
//--------------------------------------------------------------------------
//  引    数：char     *remote_address : 接続先IPアドレス文字列
//           uint16_t  port           : 接続先TCPポート番号
//  戻 り 値：true=正常 / false=異常
//**************************************************************************
static bool retry_tcp_connection(char *remote_address, uint16_t port)
{
    bool bSuccess = true;

    // 既存ソケットが残っている可能性があるため、先に明示的に切断する。
    close_connection();

    // 指定された接続先へ再接続する。
    bSuccess = open_connection(remote_address, port);
    if (bSuccess == false) {
        return false;
    }

    nrc_usr_print("retry_tcp_connection success!!! \n");
    return true;
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：TCPソケットへ指定サイズ分のデータを送信する
//--------------------------------------------------------------------------
//  引    数：char *data       : 送信データ格納先頭アドレス
//           int   data_length: 送信データサイズ
//  戻 り 値：1以上=今回最後に送信したバイト数 / 0=送信失敗 / -1=select異常
//**************************************************************************
static int send_socket_data(char *data, int data_length)
{
    int ret = 0;                    // select()戻り値
    int send_retry_count = 0;       // send()失敗時のリトライ回数
    int send_len = 0;               // send()で実際に送信できたバイト数
    int remain_len = data_length;   // 未送信の残りバイト数

    struct timeval select_timeout;  // select()待ちタイムアウト値
    fd_set fdWrite;                 // 書き込み可能監視用fd集合

    //======================================================================
    //  select()タイムアウト設定
    //======================================================================
    //  送信可能状態になるまで最大2秒待つ。
    //  注意：SOCK_TIMEOUT_SECONDS定義値は現状未使用。
    select_timeout.tv_sec = 2;
    select_timeout.tv_usec = 0;

    //======================================================================
    //  全データ送信ループ
    //======================================================================
    //  TCPではsend()一回で要求サイズすべてが送信される保証はない。
    //  そのため、送信済みバイト数だけポインタを進め、残りを継続送信する。
    while (remain_len > 0) {

        // 書き込み監視用fd集合を初期化し、現在のソケットを登録する。
        FD_ZERO(&fdWrite);
        FD_SET(sockfd, &fdWrite);

        // ソケットが書き込み可能になるまで待つ。
        ret = select(sockfd + 1, NULL, &fdWrite, NULL, &select_timeout);
        if (ret > 0) {
            if (FD_ISSET(sockfd, &fdWrite)) {
                FD_CLR(sockfd, &fdWrite);

                // 未送信データを送信する。
                send_len = send(sockfd, data, remain_len, 0);
                if (send_len < 0) {
                    // send()失敗時は、同一接続内でMAX_SEND_RETRY回まで再試行する。
                    if (send_retry_count < MAX_SEND_RETRY) {
                        send_retry_count++;
                        continue;
                    }
                    else {
                        nrc_usr_print("[%s] MAX retry (%d) reached, returning false\n",
                                      __func__, send_retry_count);
                        return 0;
                    }
                }

                // 送信成功したため、send()失敗リトライカウンタをクリアする。
                send_retry_count = 0;

                // 送信できたバイト数だけ送信ポインタを進める。
                data += send_len;
                remain_len -= send_len;
            }
        }
        else if (ret == 0) {
            // select()タイムアウト。送信可能状態にならなかった。
            nrc_usr_print("[%s] couldn't send...\n", __func__);
            return 0;
        }
        else {
            // select()自体の異常。
            nrc_usr_print("[%s] 'select' failed inside send method\n", __func__);
            return -1;
        }
    }

    // 注意：戻り値は総送信バイト数ではなく、最後のsend()で送信したバイト数。
    //       総送信バイト数が必要な場合は、別途total_send_lenを加算する実装が望ましい。
    return send_len;
}

//**************************************************************************
//  関数種別：内部参照関数
//  処理概要：TCPソケットからデータを受信する
//--------------------------------------------------------------------------
//  引    数：char *recv_buffer : 受信データ格納バッファ
//           int   buffer_len   : 受信データ格納バッファサイズ
//  戻 り 値：1以上=受信バイト数 / 0=受信データなし / -1=受信異常
//**************************************************************************
static int receive_socket_data(char *recv_buffer, int buffer_len)
{
    int ret = 0;        // select()戻り値
    int recv_len = 0;   // recv()で受信したバイト数

    struct timeval select_timeout;  // select()待ちタイムアウト値
    fd_set fdRead;                  // 読み込み可能監視用fd集合

    //======================================================================
    //  select()タイムアウト設定
    //======================================================================
    //  受信可能状態になるまで最大2秒待つ。
    //  注意：SOCK_TIMEOUT_SECONDS定義値は現状未使用。
    select_timeout.tv_sec = 2;
    select_timeout.tv_usec = 0;

    // 読み込み監視用fd集合を初期化し、現在のソケットを登録する。
    FD_ZERO(&fdRead);
    FD_SET(sockfd, &fdRead);

    // ソケットが読み込み可能になるまで待つ。
    ret = select(sockfd + 1, &fdRead, NULL, NULL, &select_timeout);

    if (ret > 0) {
        if (FD_ISSET(sockfd, &fdRead)) {
            FD_CLR(sockfd, &fdRead);

            // buffer_len - 1 を指定して受信する。
            // 文字列終端 '\0' を後段で付加する余地を残す意図と考えられる。
            // ただし、本関数内では終端文字は付加していないため、文字列扱いする場合は注意。
            recv_len = recv(sockfd, recv_buffer, buffer_len - 1, 0);
            if (recv_len <= 0) {
                // recv_len == 0 は相手側切断、recv_len < 0 は受信エラー。
                nrc_usr_print("recv failed\n");
                return -1;
            }
        }

        return recv_len;
    }
    else if (ret == 0) {
        // select()タイムアウト。受信可能データなし。
        nrc_usr_print("nothing to receive, continue...\n");
        return 0;
    }
    else {
        // select()自体の異常。
        nrc_usr_print("'select' failed inside recv method\n");
        return -1;
    }
}

//**************************************************************************
//  関数種別：外部参照可能関数
//  処理概要：TCP接続済みソケットへデータを送信し、失敗時は再接続して再送する
//--------------------------------------------------------------------------
//  引    数：char     *remote_address : 接続先IPアドレス文字列
//           uint16_t  port           : 接続先TCPポート番号
//           char     *data           : 送信データ格納先頭アドレス
//           int       data_length    : 送信データサイズ
//  戻 り 値：true=正常 / false=異常
//**************************************************************************
bool upload_data_packet(char *remote_address, uint16_t port, char *data, int data_length)
{
    int retryCount = 0;     // 再接続リトライ回数

    //======================================================================
    //  送信リトライループ
    //======================================================================
    //  send_socket_data()に失敗した場合、TCP接続を張り直して再送を試みる。
    //  retry_tcp_connection()が成功した場合はretryCountを0へ戻すため、
    //  接続復旧後の送信失敗に対して改めてMAX_RETRY回まで試行する動作になる。
    while (retryCount < MAX_RETRY) {

        // データ送信が1バイト以上成功した場合は正常終了とする。
        if (send_socket_data(data, data_length) > 0) {
            return true;
        }
        else {
            nrc_usr_print("upload_data_packet attempting retry %d\n", retryCount);

            // 送信失敗時は、ソケットを閉じてから再接続する。
            if (retry_tcp_connection(remote_address, port)) {
                nrc_usr_print("Restart upload_data_packet!!!\n");

                // 再接続に成功したため、リトライ回数をリセットする。
                retryCount = 0;
            }
            else {
                // 再接続失敗時のみ、リトライ回数を加算する。
                retryCount++;
            }
        }
    }

    // 最大リトライ回数を超過した場合は異常終了とする。
    return false;
}
