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
#include "server.h"
#include "utility.h"
#include "my_socket.h"
#include "dhcp.h"

struct proctable {
        int event;
        int status;
        void (*func) (void);
};
struct served_ip *ip_head;
struct client *cli_head;
int ip_num;//使用可能IPの数。
int crr_client; //現在のクライアント数
int status;
int s; //ソケット記述子
in_port_t myport = 51230; // 自ポート
struct sockaddr_in myskt; // 自ソケットアドレス構造体
fd_set rfds; //記述子集合
struct dhcph msg; //DHCPヘッダ
struct sockaddr_in from;
struct timeval tv;// サーバそのもののタイムアウト
struct client *temp_cli;//タイムアウト管理用
struct served_ip *temp_ip; //タイムアウト用
int status;// 今処理しているクライアントのステータス
struct timeval sig_tv = {1, 0}; //SIGALRM用
struct itimerval time_interval;
long server_timeout; //サーバ自身のタイムアウト時間

// ttlネットワークバイトオーダーに直して送信！！
// 保存はホストバイトオーダーで

/**
 * @brief SIGALRMを処理するハンドラ
 * @param s [description]
 */
void time_handler(int s) {
        // ttlをデクリメント
        decriment_ttl(cli_head);
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
 * サーバ起動時の初期化を行う。
 */
void init(char *fname) {
        mem_alloc(ip_head, struct served_ip, sizeof(struct served_ip), 1);
        mem_alloc(cli_head, struct client, sizeof(struct client), 1);
        ip_head->fp = ip_head->bp = ip_head;
        cli_head->fp = cli_head->bp = cli_head;
        ip_num = perser(ip_head, fname);
        // サーバはTTL×2sec間完全に通信がない場合終了する
        server_timeout = 2*ip_head->ttl;
        tv.tv_sec = server_timeout;
        tv.tv_usec = 0;
        crr_client = 0;
}

/**
 * @brief ソケットの初期化
 * @return [description]
 */
void init_socket() {
        if ((s = make_socket(TYPE_UDP)) < 0) {
                exit(1);
        }

        if (bind_my_ip(s, myport, &myskt) < 0) {
                exit(1);
        }
}

/**
 * @brief temp_cli, temp_ipをrecall
 */
void recall_client_ip() {
        rm_client(cli_head, temp_cli);
        mem_alloc(temp_ip, struct served_ip, sizeof(struct served_ip), 1);
        temp_ip->ip = temp_cli->addr;
        temp_ip->netmask = temp_cli->netmask;
        temp_ip->ttl = ip_head->ttl;
        insert_ip(ip_head, temp_ip);
        temp_ip = NULL;
        free(temp_cli);
        temp_cli = NULL;
        crr_client--;
}


/**
 * @brief 何らかのメッセージが来る＆タイムアウトをチェックする。
 * @return 受信したmsgのtype
 */
int wait_event() {
        int flag;
        int rsize;
        int timeouts;
        struct client *addc;

        // 使用期限タイムアウトを確認
        if (crr_client) {
                temp_cli = timeout_client(cli_head, ip_head);
                if (temp_cli != NULL) {
                        switch (temp_cli->stat) {
                        case STAT_IN_USE:
                                //該当クライアントを取り除く
                                // in use -> end
                                fprintf(stderr, "TIMEOUT: ");
                                fprintf(stderr, "recall %s", inet_ntoa(temp_cli->id));
                                fprintf(stderr, "(%s)\n", inet_ntoa(temp_cli->addr));
                                fprintf(stderr, "[in use -> end]\n");
                                recall_client_ip();
                                status = SKIP;
                                return SKIP;
                        case STAT_RESEND_OFFER:
                                //該当クライアントを取り除く
                                // resend offer -> end
                                fprintf(stderr, "REQUEST TIMEOUT: ");
                                fprintf(stderr, "recall %s", inet_ntoa(temp_cli->id));
                                fprintf(stderr, "(%s)\n", inet_ntoa(temp_cli->addr));
                                fprintf(stderr, "[resend offer -> end]\n");
                                recall_client_ip();
                                status = SKIP;
                                return SKIP;
                        case STAT_WAIT_REQ:
                                // OFFERを再送
                                // wait req -> resend offer
                                fprintf(stderr, "REQUEST TIMEOUT: ");
                                fprintf(stderr, "resend offer to %s", inet_ntoa(temp_cli->id));
                                fprintf(stderr, "(%s)\n", inet_ntoa(temp_cli->addr));
                                fprintf(stderr, "[wait req -> resend offer]\n");
                                memset(&from, 0, sizeof(from));
                                from.sin_family = AF_INET;
                                from.sin_port = temp_cli->port;
                                from.sin_addr = temp_cli->id;
                                temp_cli->ttlcounter = 10;
                                return T_OFFER;
                        }
                }
        }
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
                else {
                        status = SKIP;
                        return SKIP;
                }
        } else if (flag == 0) {
                // サーバ自身のタイムアウト
                return TIME_OUT;
        }

        if (FD_ISSET(s, &rfds)) {
                // 何らかのメッセージを受信した。
                tv.tv_sec = server_timeout;
                memset(&msg, 0, sizeof(msg));
                memset(&from, 0, sizeof(from));
                rsize = recv_udp(s, &msg, &from, sizeof(struct dhcph));

                print_message(msg);
                if (msg.type < T_DISCOVER || msg.type > T_RELEASE) {
                        // 不正なメッセージをチェック(Typeによって判定)
                        // 不正なメッセージ
                        return UNKNOWN_MSG;
                }
                if ((addc = find_client(cli_head, from.sin_addr.s_addr)) != NULL)
                        status = addc->stat;
                else
                        status = STAT_WAIT_OFFER; // 初めてのクライアント
                return msg.type;
        }
}

/**
 * @brief OFFERを送信
 * @return [description]
 */
void send_offer() {
        struct served_ip *temp;
        struct client *addc;

        memset(&msg, 0, sizeof(msg));
        msg.type = T_OFFER;
        if (crr_client >=ip_num) {
                // 使用可能なIPナシ
                // init -> end
                printf("[init -> end]\n");
                msg.code = CANNOT_ALLOC_IP;
                send_udp(s, &msg, sizeof msg, &from);
                print_message(msg);
                return;
        }

        if ((addc = find_client(cli_head, from.sin_addr.s_addr)) != NULL) {
                // すでにクライアントにあるのにディスカバーを送ってきた。
                fprintf(stderr, "Invalid DISCOVER: client %s already exists.\n",
                        inet_ntoa(addc->id));
                return;
        }

        // 使用可能なIPアリ.
        temp = rm_ip(ip_head);
        mem_alloc(addc, struct client, sizeof(struct client), 1);

        msg.code = CAN_ALLOC_IP;
        msg.ttl = htons(temp->ttl);
        addc->ttlcounter = 10; // ttlcounter = 10 かつstat = STAT_WAIT_ACKのとき
        // タイムアウトしたならofferを再送。
        addc->addr = temp->ip;
        addc->netmask = temp->netmask;
        msg.address = temp->ip.s_addr;
        msg.netmask = temp->netmask.s_addr;
        addc->id = from.sin_addr;
        addc->port = from.sin_port;
        addc->stat = STAT_WAIT_REQ;
        /* 割り当てたアドレス情報を表示*/
        printf("-------IP ALLOCATED--------\n");
        printf("  IP:      %s\n", inet_ntoa(addc->addr));
        printf("  NetMASK: %s\n", inet_ntoa(addc->netmask));
        printf("  TTL:     %" PRIu16 "\n", temp->ttl);
        printf("---------------------------\n\n");
        insert_client(cli_head, addc);
        crr_client++;
        free(temp);

        send_udp(s, &msg, sizeof msg, &from);
        printf("[init -> wait req]\n");
        print_message(msg);
}

/**
 * @brief 割り当て要求に対応するACKを返す。
 */
void allocate_ack() {
        // 該当クライアントを見つける
        temp_cli = find_client(cli_head, from.sin_addr.s_addr);
        if (temp_cli == NULL) {
                fprintf(stderr, "Error: There is no such client %s\n", inet_ntoa(from.sin_addr));
                return;
        }

        if (temp_cli->addr.s_addr != msg.address ||
            temp_cli->netmask.s_addr != msg.netmask || ntohs(msg.ttl) > ip_head->ttl ||
            ntohs(msg.ttl) <= 0) {
                // wait req -> end
                // resend offer -> end
                fprintf(stderr, "Invalid REQUEST\n");
                fprintf(stderr, "recall %s", inet_ntoa(temp_cli->id));
                fprintf(stderr, "(%s)\n", inet_ntoa(temp_cli->addr));
                printf("[%s -> end]\n", stat2str(temp_cli->stat));
                memset(&msg, 0, sizeof(msg));
                msg.type = T_ACK;
                msg.code = REQUEST_ERR;
                send_udp(s, &msg, sizeof msg, &from);
                recall_client_ip();
                return;
        }

        // 割り当て要求を受理。ACKを返す。
        temp_cli->ttlcounter = temp_cli->ttl = ntohs(msg.ttl);
        memset(&msg, 0, sizeof(msg));
        msg.type = T_ACK;
        msg.code = ACK_OK;
        msg.ttl = htons(temp_cli->ttl);
        msg.address = temp_cli->addr.s_addr;
        msg.netmask = temp_cli->netmask.s_addr;
        printf("[%s -> in use]\n", stat2str(temp_cli->stat));
        temp_cli->stat = STAT_IN_USE;
        temp_cli = NULL;
        send_udp(s, &msg, sizeof msg, &from);
        print_message(msg);
        return;
}

/**
 * @brief RELEASEを受信したときの処理
 */
void recv_release() {
        struct client *temp;
        struct served_ip *addip;

        // 該当クライアントを見つける
        temp = find_client(cli_head, from.sin_addr.s_addr);

        if (temp == NULL) {
                fprintf(stderr, "Error: There is no such client %s\n", inet_ntoa(from.sin_addr));
                return;
        }

        if (temp->addr.s_addr != msg.address) {
                struct in_addr temp;
                temp.s_addr = msg.address;
                fprintf(stderr, "Invalid message address: %s\n", inet_ntoa(temp));
        }
        // 該当クライアントを取り除く
        temp = rm_client(cli_head, temp);
        crr_client--;
        mem_alloc(addip, struct served_ip, sizeof(struct served_ip), 1);
        addip->ip = temp->addr;
        addip->netmask = temp->netmask;
        addip->ttl = ip_head->ttl;
        insert_ip(ip_head, addip);
        free(temp);
}

void skip() {
// Do nothing
}

/**
 * @brief OFFERを再送信
 * @return [description]
 */
void resend_offer() {
        memset(&msg, 0, sizeof(msg));
        msg.type = T_OFFER;
        msg.code = CAN_ALLOC_IP;
        msg.ttl = htons(ip_head->ttl);
        temp_cli->ttlcounter = 10;
        temp_cli->stat = STAT_RESEND_OFFER;
        msg.address = temp_cli->id.s_addr;
        msg.netmask = temp_cli->netmask.s_addr;
        // fromはwait_event側で用意
        printf("[wait req -> resend offer]\n");
        send_udp(s, &msg, sizeof msg, &from);
        print_message(msg);
}

int main(int argc, char **argv) {
        struct proctable ptab[] = {
                // TODO
                {T_DISCOVER, STAT_WAIT_OFFER, send_offer}, // init -> wait req
                {T_REQUEST, STAT_WAIT_REQ, allocate_ack}, // wait -> in use
                {T_REQUEST, STAT_IN_USE, allocate_ack}, // in use -> in use
                {T_RELEASE, STAT_IN_USE, recv_release}, // in use -> end
                {T_OFFER, STAT_WAIT_REQ, resend_offer}, // wait req -> resend offer
                {T_REQUEST, STAT_RESEND_OFFER, allocate_ack}, // resend offer -> in use
                {SKIP, SKIP, skip}
        };
        struct proctable *pt;
        int event;
        //エラーチェック
        if (argc != 2) {
                fprintf(stderr, "Usage dhcps <config-file name>\n");
                exit(1);
        }

        init(argv[1]);
        print_config(ip_head);
        //ソケットを初期化
        init_socket();

        //インターバルタイマをセット
        time_interval.it_interval = time_interval.it_value = sig_tv;
        setitimer(ITIMER_REAL, &time_interval, NULL);
        while (1) {
                cause_alrm();
                pause();

                if ((event = wait_event()) == TIME_OUT) {
                        // サーバ自信を終了
                        fprintf(stderr, "Terminated Server. Bye Bye\n");
                        exit(0);
                } else if (event == UNKNOWN_MSG) {
                        /* typeが不正なメッセージ*/
                        if ((temp_cli = find_client(cli_head, from.sin_addr.s_addr)) != NULL) {
                                // クライアントリストにあるなら除去
                                printf("[%s -> end]", stat2str(temp_cli->stat));
                                recall_client_ip();
                                fprintf(stderr, "ERROR: Invalid message type\n");
                                continue;
                        }
                        printf("[init -> end]\n");
                        fprintf(stderr, "ERROR: Invalid message type\n");
                        continue;
                }
                for (pt = ptab; pt->status; pt++) {
                        if (pt->status == status &&
                            pt->event == event) {
                                (*pt->func)();
                                break;
                        }
                        if (pt->status == 0) {
                                fprintf(stderr, "unexpected event\n");
                                close(s);
                                exit(1);
                        }
                }
        }
}
