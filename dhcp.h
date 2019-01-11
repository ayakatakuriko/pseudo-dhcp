#ifndef DHCP_H
#define DHCP_H

/* パケットフォーマット*/
struct dhcph {
        uint8_t type; // メッセージの種類
        uint8_t code;
        uint16_t ttl;
        in_addr_t address;
        in_addr_t netmask;
};

/* Type*/
#define T_DISCOVER 0x01
#define T_OFFER 0x02
#define T_REQUEST 0x03
#define T_ACK 0x04
#define T_RELEASE 0x05
#define UNKNOWN_MSG 0x06

/* Code*/
#define CAN_ALLOC_IP 0 // OFFERについて、割り当て可能なIPアリ
#define CANNOT_ALLOC_IP 1 // OFFERについて、割り当て可能なIPナシ
#define ALLOC_REQ 2 // REQUESTについて割り当て要求
#define EXTEND_TIME_REQ 3 // 使用期間延長要求
#define ACK_OK 0//ACKについて割り当てOK
#define REQUEST_ERR 4 // ACKで使用。REQUESTに誤りがあった

/* クライアントのstatus*/
#define STAT_INIT 0x13
#define STAT_WAIT_OFFER 0x01
#define STAT_WAIT_ACK 0x02
#define STAT_IN_USE 0x03
#define STAT_WAIT_EXTEND 0x04
#define SKIP 0x16 //やることがない
#define STAT_RELEASE 0x06 //RELEASEを送る必要がある
/* クライアントのstatus。以下のものはタイムアウトした場合の遷移先がterminated*/
#define STAT_RESEND_OFFER 0x07 //REQUESTが帰ってこないので、再送
#define STAT_TERMINATED 0x08 // 終了
#define STAT_RESEND_DISCOVER 0x09
#define STAT_RESEND_REQUEST 0x10
#define STAT_RESEND_EXTEND 0x11
#define STAT_WAIT_REQ 0x12


/**
 * @brief パケットの内容を出力する
 * @param msg 出力したいパケット
 */
void print_message(struct dhcph msg);
/**
 * @brief ステータスを文字列に変更する
 * @param  stat [description]
 * @return      [description]
 */
char *stat2str(int stat);

#endif
