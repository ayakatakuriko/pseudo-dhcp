#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include "utility.h"
#include "my_socket.h"
#include "dhcp.h"

struct proctable {
        int event;
        int status;
        void (*func) (void);
};
int status;// 今の状態
int s; //ソケット記述子
in_port_t myport = 51230;
in_port_t port = 51230;
struct sockaddr_in myskt, dist; // 自ソケットアドレス構造体
struct dhcph msg; //DHCPヘッダ
int get_flag = 0; //アドレスを取得している場合は１
struct timeval tv = {10, 0};// メッセージ受信のタイムアウト
fd_set rfds; //記述子集合
int alrm_flag = 0;
int first_ack = 0; //ACKを受け取ったことがないなら0
struct timeval sig_tv = {1, 0}; //SIGALRM用
struct itimerval time_interval;
/**以下サーバによって割り当てられるもの*/
uint16_t ttlcounter;
// below: network byte order
struct in_addr addr;
struct in_addr netmask;
uint16_t ttl; // ホストオーダーで保存

/**
 * @brief 取得したIPの情報を表示する。
 */
void print_allocated_ip() {
        printf("-------IP ALLOCATED--------\n");
        printf("  IP:      %s\n", inet_ntoa(addr));
        printf("  NetMASK: %s\n", inet_ntoa(netmask));
        printf("  TTL:     %" PRIu16 "\n", ttl);
        printf("---------------------------\n\n");
}

/**
 * @brief SIGALRMを処理するハンドラ
 * @param s [description]
 */
void time_handler(int s) {
        alrm_flag++;
        if (first_ack) {
                // ttlcounter が設定済み
                ttlcounter-= 1;
                printf("TTL: %" PRIu16 "\n", ttlcounter);
        }
}

/**
 * @brief SIGALRMに対する処理
 */
void cause_alrm() {
        struct sigaction act;

        memset(&act, 0, sizeof act);
        sigemptyset(&act.sa_mask);
        act.sa_handler = time_handler;
        act.sa_flags = SA_RESTART;
        if (sigaction(SIGALRM, &act, NULL)) {
                perror("SIGALRM");
                exit(1);
        }
}

/**
 * @brief SIGHUPを処理するハンドラ
 * @param s [description]
 */
void sighup_handler(int s) {
        status = STAT_RELEASE;
        fprintf(stderr, "SIGHUP!!!!!\n");
}

/**
 * @brief SIGHUPに対する処理
 */
void handle_sighup() {
        struct sigaction act;

        memset(&act, 0, sizeof act);
        sigemptyset(&act.sa_mask);
        act.sa_handler = sighup_handler;
        act.sa_flags = SA_RESTART;
        if (sigaction(SIGHUP, &act, NULL)) {
                perror("SIGALRM");
                exit(1);
        }
}

/**
 * @brief DISCOVERを送信する
 */
void send_discover() {
        memset(&msg, 0, sizeof(msg));
        msg.type = T_DISCOVER;
        send_udp(s, &msg, sizeof msg, &dist);
        status = STAT_WAIT_OFFER;
        printf("[init -> wait offer]\n");
        print_message(msg);
}

/**
 * @brief DISCOVERを再送する
 */
void resend_discover() {
        memset(&msg, 0, sizeof(msg));
        msg.type = T_DISCOVER;
        send_udp(s, &msg, sizeof msg, &dist);
        printf("[wait offer -> resend discover]\n");
        print_message(msg);
}

/**
 * @brief IPアドレスを要求するREQUESTを送信する。
 */
void send_ip_request() {
        /* 割り当てられる予定のアドレスとネットマスクを格納*/
        addr.s_addr = msg.address;
        netmask.s_addr = msg.netmask;
        ttlcounter = ttl = ntohs(msg.ttl);
        /* REQUESTを送信*/
        memset(&msg, 0, sizeof(msg));
        msg.type = T_REQUEST;
        msg.code = ALLOC_REQ;
        msg.address = addr.s_addr;
        msg.netmask = netmask.s_addr;
        msg.ttl = htons(ttl);
        send_udp(s, &msg, sizeof msg, &dist);
        printf("[%s -> wait ack]\n", stat2str(status));
        status = STAT_WAIT_ACK;
        print_message(msg);
}

/**
 * @brief IPアドレスを要求するREQUESTを再送する。
 */
void resend_ip_request() {
        /* REQUESTを送信*/
        memset(&msg, 0, sizeof(msg));
        msg.type = T_REQUEST;
        msg.code = ALLOC_REQ;
        msg.address = addr.s_addr;
        msg.netmask = netmask.s_addr;
        msg.ttl = htons(ttl);
        send_udp(s, &msg, sizeof msg, &dist);
        printf("[wait ack -> resend ack]\n");
        print_message(msg);
}

/**
 * @brief 使用時間延長要求を行う。
 */
void send_extend_request() {
        /* REQUESTを送信*/
        memset(&msg, 0, sizeof(msg));
        msg.type = T_REQUEST;
        msg.code = EXTEND_TIME_REQ;
        msg.address = addr.s_addr;
        msg.netmask = netmask.s_addr;
        msg.ttl = htons(ttl);
        send_udp(s, &msg, sizeof msg, &dist);
        status = STAT_WAIT_EXTEND;
        tv.tv_sec = ttl / 2; // selectはtvを更新する
        printf("[in use -> wait ext ack]\n");
        print_message(msg);
        get_flag = 0;
}

/**
 * @brief 使用時間延長要求を行う。
 */
void resend_extend_request() {
        /* REQUESTを送信*/
        memset(&msg, 0, sizeof(msg));
        msg.type = T_REQUEST;
        msg.code = EXTEND_TIME_REQ;
        msg.address = addr.s_addr;
        msg.netmask = netmask.s_addr;
        msg.ttl = htons(ttl);
        send_udp(s, &msg, sizeof msg, &dist);
        print_message(msg);
        get_flag = 0;
}

/**
 * @brief ACKを受け取ったときの処理を行う
 */
void recv_ack() {
        get_flag = 1;
        ttlcounter = ttl;
        if (!first_ack) {
                // はじめてのACK
                first_ack++;
                print_allocated_ip();
        }
        printf("[%s -> in use]\n", stat2str(status));
        status = STAT_IN_USE;
}

/**
 * @brief RELEASEを送信
 */
void send_release() {
        /* RELEASEを送信*/
        memset(&msg, 0, sizeof(msg));
        msg.type = T_RELEASE;
        msg.address = addr.s_addr;
        send_udp(s, &msg, sizeof msg, &dist);
        printf("SEND\n");
        print_message(msg);
        status = STAT_WAIT_EXTEND;
        printf("Connection terminated\n");
}

int wait_event() {
        int flag;
        int rsize;

        // SIGALRMを待つ
        cause_alrm();
        if (!get_flag) { //アドレス取得前
                // メッセージ待ち
                FD_ZERO(&rfds);
                FD_SET(s, &rfds);
                flag = select(s+1, &rfds, NULL, NULL, &tv);
                if (flag < 0) {
                        // errnoがSIGALRMによるものであるなら無視
                        // 参考：https://stackoverflow.com/questions/23566545/signal-and-ualarm-in-conflict-with-select
                        if (errno != EINTR) {
                                perror("select");
                                exit(errno);
                        }
                        else{
                                return SKIP;
                        }
                } else if (flag == 0) {
                        // タイムアウト
                        tv.tv_sec = 10; // selectはtvを更新する
                        fprintf(stderr, "TIMEOUT: ");
                        switch (status) {
                        case STAT_WAIT_OFFER:
                                // OFFERが来ないので、DISを再送
                                // wait offer -> resend offer
                                status = STAT_RESEND_DISCOVER;
                                return T_DISCOVER;
                        case STAT_WAIT_ACK:
                                // ACKが来ないのでREQUESTを再送
                                fprintf(stderr, "Resend IP request\n");
                                status = STAT_RESEND_REQUEST;
                                return T_REQUEST;
                        case STAT_WAIT_EXTEND:
                                fprintf(stderr, "Timeout\n");
                                fprintf(stderr, "Terminated Client. Bye Bye\n");
                                close(s);
                                return STAT_TERMINATED;
                        case STAT_RESEND_DISCOVER:
                        case STAT_RESEND_REQUEST:
                                fprintf(stderr, "Resend message timeout\n");
                                fprintf(stderr, "Terminated Client. Bye Bye\n");
                                close(s);
                                return STAT_TERMINATED;
                        }
                        // TODO
                        exit(1);
                }

                if (FD_ISSET(s, &rfds)) {
                        // 何らかのメッセージを受信した。
                        tv.tv_sec = 10; // selectはtvを更新する
                        memset(&msg, 0, sizeof(msg));
                        rsize = recv_udp(s, &msg, &dist, sizeof(struct dhcph));
                        print_message(msg);
                        if (msg.type < T_DISCOVER && msg.type > T_RELEASE) {
                                //TODO:不正なメッセージをチェック(Typeによって判定)
                                // 不正なメッセージ
                                return UNKNOWN_MSG;
                        }
                        if (msg.type == T_OFFER && msg.code == CANNOT_ALLOC_IP) {
                                // IPの割り当てを拒否された
                                fprintf(stderr, "OFFER: Cannot allocated TP\n");
                                return STAT_TERMINATED;
                        }
                        if (msg.type == T_ACK && msg.code == REQUEST_ERR) {
                                // ACK: エラー
                                fprintf(stderr, "ACK ERROR: Invalid REQUEST\n");
                                return STAT_TERMINATED;
                        }
                        return msg.type;
                }
        } else { // アドレス取得後
                // SIGHUPを待つ
                handle_sighup();
                pause();
                if (alrm_flag > 0) {
                        alrm_flag = 0;
                        if (ttlcounter <= (ttl / 2)) {
                                // 延長要求を送る
                                return T_REQUEST;
                        }
                }

                /* SIGHUPを受信*/
                if (status == STAT_RELEASE) {
                        send_release();
                        close(s);
                        return STAT_TERMINATED;
                }
                return SKIP;
        }
}

void skip() {
// Do nothing
}

// TODO: メッセージ間のタイムアウトの実装
int main(int argc, char **argv) {
        struct proctable ptab[] = {
                // TODO
                {T_OFFER, STAT_WAIT_OFFER, send_ip_request}, // wait offer -> wait ack
                {T_ACK, STAT_WAIT_ACK, recv_ack}, // wait ack -> in use
                {T_ACK, STAT_WAIT_EXTEND, recv_ack}, // wait ext ack -> in use
                {T_REQUEST, STAT_IN_USE, send_extend_request}, // in use -> wait ext ack
                {T_DISCOVER, STAT_RESEND_DISCOVER, resend_discover}, // wait offer -> resend discover
                {T_OFFER, STAT_RESEND_DISCOVER, send_ip_request}, // resend discover -> wait ack
                {T_REQUEST, STAT_RESEND_REQUEST, resend_ip_request}, // wait ack -> resend ack
                {T_ACK, STAT_RESEND_REQUEST, recv_ack}, // resend ack -> in use
                {SKIP, STAT_IN_USE, skip},
                {0, 0, NULL}
        };
        struct proctable *pt;
        int event;

        //エラーチェック
        if (argc != 2) {
                fprintf(stderr, "Usage: mydhcpc <IP address>\n");
                exit(1);
        }

        // ソケットを割り当て
        s = make_socket(TYPE_UDP);
        bind_my_ip(s, myport, &myskt);
        init_specific_ip(port, &dist, argv[1]);
        // DISCOVERを送信
        send_discover();

        //インターバルタイマをセット
        time_interval.it_interval = time_interval.it_value = sig_tv;
        setitimer(ITIMER_REAL, &time_interval, NULL);
        while(1) {
                event = wait_event();
                if (event == SKIP)
                        continue; // selectがSIGSLRMによって中断したりするとき
                else if (event == STAT_TERMINATED) {
                        //タイムアウトによる終了
                        printf("[%s -> end]\n", stat2str(status));
                        exit(1);
                }

                for (pt = ptab; pt->status; pt++) {
                        if (pt->status == status && pt->event == event) {
                                (*pt->func)();
                                break;
                        }
                }
                if (pt->status == 0) {
                        fprintf(stderr, "ERROR: invalid message\n");
                        exit(1);
                }
        }

        close(s);
}
