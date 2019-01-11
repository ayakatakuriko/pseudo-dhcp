#ifndef SERVER_H
#define SERVER_H

#define STR_LEN 20

/* 状態遷移のイベント*/
#define TIME_OUT 4


struct client {
        struct client *fp; // 次の要素を指す
        struct client *bp; // 前の要素を指す
        int stat;
        int ttlcounter;
        // below: network byte order
        struct in_addr id;
        struct in_addr addr;
        struct in_addr netmask;
        in_port_t port;
        uint16_t ttl;
};

/*クライアントに提供するＩＰアドレスを双方向リストで管理*/
struct served_ip {
        struct served_ip *fp;
        struct served_ip *bp;
        struct  in_addr ip;
        struct  in_addr netmask;
        uint16_t ttl;
};

/**
 * @biref クライアントを双方向リストの最後に入れる
 * @param head
 * @param target
 * */
void insert_client(struct client *head, struct client *target);

/**
 * @brief 特定のクライアントを双方向リストから取り除く
 * @param head 双方向リストの先頭
 * @param target 消去したいクライアント
 * @return 消去したクライアントへのアドレス.targetが無かったらNULLを返す。
 * */
struct client *rm_client(struct client *head, struct client *target);

/**
 * @brief ＩＰアドレスをリストの最後に挿入する。
 * @param head
 * @param target
 **/
void insert_ip(struct served_ip *head, struct served_ip *target);

/**
 * @brief リストの先頭のＩＰアドレスを取り除く。
 **/
struct served_ip *rm_ip(struct served_ip *head);

/**
 * @brief configファイルからデータを読み込む
 * @param head
 * @return 読み込んだＩＰとネットマスクの組数
 **/
int perser(struct served_ip *head, char *fname);

/**
 * @brief コンフィグファイルの内容を出力
 * @param head
 */
void print_config(struct served_ip *head);

/**
 * @brief タイムアウトしたクライアントを取り除く
 * @param  chead [description]
 * @return クライアント
 */
struct client *timeout_client(struct client *chead, struct served_ip *phead);

/**
 * @brief ipをもとにクライアントを探索する
 * @param head 双方向リストの先頭
 * @param id 探したいid. ネットワークバイトオーダー。
 * @return 見つけたクライアントへのアドレス.targetが無かったらNULLを返す。
 * */
struct client *find_client(struct client *head, in_addr_t id);

/**
 * @brief リストの先頭にあるIPアドレスを読む
 * @param  head [description]
 * @return      [description]
 */
struct served_ip *get_ip(struct served_ip *head);

/**
 * @brif クライアントリストのttlカウンターをデクリメント
 * @param head クライアントリストの先頭
 */
void decriment_ttl(struct client *head);
#endif
