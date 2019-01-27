/**
 *@brief DHCP server
 *@author ayakatakuriko
 *@date 2018/12/14
 * */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include "server.h"
#include "utility.h"
#include "dhcp.h"


/**
 * @biref クライアントを双方向リストの最後に入れる
 * @param head
 * @param target
 * */
void insert_client(struct client *head, struct client *target) {
        target->fp = head;
        target->bp = head->bp;
        head->bp->fp = target;
        head->bp = target;
}

/**
 * @brief 特定のクライアントを双方向リストから取り除く
 * @param head 双方向リストの先頭
 * @param target 消去したいクライアント
 * @return 消去したクライアントへのアドレス.targetが無かったらNULLを返す。
 * */
struct client *rm_client(struct client *head, struct client *target) {
        struct client *p;

        if (target == NULL) return NULL;
        for (p = head->fp; p != head; p = p->fp) {
                if (p->id.s_addr == target->id.s_addr) {
                        p->bp->fp = p->fp;
                        p->fp->bp = p->bp;
                        p->fp = p->bp = NULL;
                        return p;
                }
        }
        return NULL;
}

/**
 * @brief ＩＰアドレスをリストの最後に挿入する。
 * @param head
 * @param target
 **/
void insert_ip(struct served_ip *head, struct served_ip *target) {
        target->fp = head;
        target->bp = head->bp;
        head->bp->fp = target;
        head->bp = target;
}

/**
 * @brief リストの先頭のＩＰアドレスを取り除く。
 **/
struct served_ip *rm_ip(struct served_ip *head) {
        struct served_ip *target = head->fp;
        target->bp->fp = target->fp;
        target->fp->bp = target->bp;
        target->fp = target->bp = NULL;
        return target;
}

/**
 * @brief configファイルからデータを読み込む
 * @param head
 * @return 読み込んだＩＰとネットマスクの組数
 **/
int perser(struct served_ip *head, char *fname) {
        FILE *fd;
        uint16_t ttl;
        struct served_ip *temp;
        char ip[STR_LEN], netmask[STR_LEN];
        int count = 0;
        int check;

        // ファイルを開く
        file_open(fd, fname, "r", 1);

        // ＴＴＬを読み込む
        if ((check = fscanf(fd, "%" PRIu16, &(head->ttl))) == EOF) {
                fprintf(stderr, "Error: Invalid config file\n");
                fclose(fd);
                return count;
        }

        while ((check = fscanf(fd, "%s %s", ip, netmask)) != EOF) {
                // 保存先のserved_ipのメモリを確保
                mem_alloc(temp, struct served_ip, sizeof(struct served_ip), 1);
                // IPとネットマスクの表示形式を変換
                inet_aton(ip, &(temp->ip));
                inet_aton(netmask, &(temp->netmask));
                temp->ttl = head->ttl;
                insert_ip(head, temp);
                temp = NULL;
                count++;
        }
        fclose(fd);
        return count;
}

/**
 * @brief コンフィグファイルの内容を出力
 * @param head
 */
void print_config(struct served_ip *head) {
        struct served_ip *p;

        if (head == NULL) {
                // エラーチェック
                fprintf(stderr, "IP list is NULL\n");
                return;
        }

        printf("\n---------- config-file information ----------\n\n");
        printf("TTL: %" PRIu16 "\n", head->ttl);
        printf("      ----- Served IP and Netmask -----\n");
        for (p = head->fp; p != head; p = p->fp) {
                printf("%s   ", inet_ntoa(p->ip));
                printf("%s\n", inet_ntoa(p->netmask));
        }
        printf("-------------------- end --------------------\n\n");
}


/**
 * @brief タイムアウトしたクライアントを返す
 * @param  chead [description]
 * @return タイムアウトしたクライアント
 */
struct client *timeout_client(struct client *chead, struct served_ip *phead) {
        struct client *p;
        struct served_ip *target;

        for (p = chead->fp; p != chead; p = p->fp) {
                if (p->ttlcounter <= 0) {
                        return p;
                }
        }
        return NULL;
}

/**
 * @brief idをもとにクライアントを探索する
 * @param head 双方向リストの先頭
 * @param id 探したいid. ネットワークバイトオーダー。
 * @return 見つけたクライアントへのアドレス.targetが無かったらNULLを返す。
 * */
struct client *find_client(struct client *head, in_addr_t id) {
        struct client *p;

        for (p = head->fp; p != head; p = p->fp) {
                if (p->id.s_addr == id)
                        return p;
        }
        return NULL;
}

/**
 * @brief リストの先頭にあるIPアドレスを読む
 * @param  head [description]
 * @return      [description]
 */
struct served_ip *get_ip(struct served_ip *head) {
        return head->fp;
}

/**
 * @brif クライアントリストのttlカウンターをデクリメント
 * @param head クライアントリストの先頭
 */
void decriment_ttl(struct client *head) {
        struct client *p;

        for (p = head->fp; p != head; p = p->fp) {
                if (p->ttlcounter > 0)
                        p->ttlcounter--;
                if (p->stat == STAT_IN_USE) {
                        printf("TTL %s(", inet_ntoa(p->addr));
                        printf("%s): %" PRIu16 " sec\n", inet_ntoa(p->id), p->ttlcounter);
                }
        }
}
