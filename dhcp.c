#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <inttypes.h>
#include "dhcp.h"

/**
 * @brief パケットの内容を出力する
 * @param msg 出力したいパケット
 */
void print_message(struct dhcph msg) {
        int flag = 0; // TTlとIPを表示するなら1
        printf("\n------------- message -------------\n");
        switch (msg.type) {
        case T_DISCOVER:
                printf("type: DISCOVER\n");
                break;
        case T_OFFER:
                printf("type: OFFER\n");
                if (msg.code == CAN_ALLOC_IP) {
                        printf("code: There is IP address that can be assigned.\n");
                        flag = 1;
                }
                else if (msg.code == CANNOT_ALLOC_IP)
                        printf("code: There is no IP address that can be assigned.\n");
                break;
        case T_REQUEST:
                printf("type: REQUEST\n");
                if (msg.code == ALLOC_REQ)
                        printf("code: Assign request\n");
                else if (msg.code == EXTEND_TIME_REQ)
                        printf("code: Time extension request\n");
                flag = 1;
                break;
        case T_ACK:
                printf("type: ACK\n");
                if (msg.code == ACK_OK) {
                        printf("code: ACK OK\n");
                        flag = 1;
                }
                else if (msg.code == REQUEST_ERR)
                        printf("code: Invalid REQUEST\n");
                break;
        case T_RELEASE:
                printf("type: RELEASE\n");
                break;
        default:
                fprintf(stderr, "type: UNKNOWN MESSAGE\n");
        }
        if (flag) {
                /* IPアドレスとTTLを表示*/
                struct in_addr temp1;
                struct in_addr temp2;
                temp1.s_addr = msg.address;
                temp2.s_addr = msg.netmask;
                printf("IP address: %s", inet_ntoa(temp1));
                printf(" netmask: %s\n", inet_ntoa(temp2));
                printf("TTL: %" PRIu16 "sec\n", ntohs(msg.ttl));
        }
        printf("-----------------------------------\n\n");
}

/**
 * @brief ステータスを文字列に変更する。ガバ実装
 * @param  stat [description]
 * @return      [description]
 */
char *stat2str(int stat) {
        switch (stat) {
        case STAT_INIT:
                return "init";
        case STAT_WAIT_REQ:
                return "wait req";
        case STAT_RESEND_OFFER:
                return "resend offer";
        case STAT_IN_USE:
                return "in use";
        case STAT_WAIT_ACK:
                return "wait ack";
        case STAT_WAIT_EXTEND:
                return "wait ext ack";
        case STAT_RESEND_DISCOVER:
                return "resend discover";
        case STAT_WAIT_OFFER:
                return "wait offer";
        case STAT_RESEND_REQUEST:
                return "resend request";
        }
}
