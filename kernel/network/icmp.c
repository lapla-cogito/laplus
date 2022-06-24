/**
 * @file icmp.c
 *
 * @brief ICMP(Internet Control Message Protocol)の実装
 *
 * @note
 * IPパケットが目的ホストまで到達したかを確認したりIPパケットの破棄を通知したりする.IP単独ではエラーを通知できない.
 */
#include "icmp.h"
#include "benri.h"
#include "ip.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ICMP_BUFSIZ IP_PAYLOAD_SIZE_MAX

/**ICMPヘッダ*/
struct icmp_hdr {
    uint8_t type; /**ICMPのタイプ
    see also: http://www.iana.org/assignments/icmp-parameters
    0: エコー応答
    3: 到達不能
    5: リダイレクト(送信元ホストが最適でない送信経路使用)
    8: エコー要求
    9: ルータ通知
    10: ルータ要求
    11: 時間超過(ttl超過, tracerouteはこれを応用)
    12: パラメータ異常
    13: タイムスタンプ応答
    14: タイムスタンプ要求
    42: 拡張エコー要求
    43: 拡張エコー応答
    */
    uint8_t
        code; /**ICMPタイプによって定義が異なる(特にタイプ3の到達不能の際に活躍)*/
    uint16_t sum;    /**checksum*/
    uint32_t values; /**メッセージ固有の値*/
};

/**ICMPの(拡張)エコー応答,(拡張)エコー要求メッセージ構造体(pingはこれを応用)*/
struct icmp_echo {
    uint8_t type;
    uint8_t code;
    uint16_t sum;
    uint16_t id;
    uint16_t seq;
};

static const char *icmp_type_ntoa(uint8_t type) {
    switch(type) {
    case ICMP_TYPE_ECHOREPLY:
        return "EchoReply";
    case ICMP_TYPE_DEST_UNREACH:
        return "DestinationUnreachable";
    case ICMP_TYPE_SOURCE_QUENCH:
        return "SourceQuench";
    case ICMP_TYPE_REDIRECT:
        return "Redirect";
    case ICMP_TYPE_ECHO:
        return "Echo";
    case ICMP_TYPE_TIME_EXCEEDED:
        return "TimeExceeded";
    case ICMP_TYPE_PARAM_PROBLEM:
        return "ParameterProblem";
    case ICMP_TYPE_TIMESTAMP:
        return "Timestamp";
    case ICMP_TYPE_TIMESTAMPREPLY:
        return "TimestampReply";
    case ICMP_TYPE_INFO_REQUEST:
        return "InformationRequest";
    case ICMP_TYPE_INFO_REPLY:
        return "InformationReply";
    default:
        return "Unknown";
    }
}

/**ICMPデバッグ出力*/
static void icmp_dump(const uint8_t *data, size_t len) {
    struct icmp_hdr *hdr;
    struct icmp_echo *echo;

    flockfile(stderr);
    hdr = (struct icmp_hdr *)data;
    printk("       type: %u (%s)\n", hdr->type, icmp_type_ntoa(hdr->type));
    printk("       code: %u\n", hdr->code);
    printk("        sum: 0x%04x (0x%04x)\n", ntoh16(hdr->sum),
           ntoh16(cksum16((uint16_t *)data, len, -hdr->sum)));
    switch(hdr->type) {
    /**エコー要求とエコー応答の場合の処理*/
    case ICMP_TYPE_ECHOREPLY:
    case ICMP_TYPE_ECHO:
        echo = (struct icmp_echo *)hdr;
        printk("         id: %u\n", ntoh16(echo->id));
        printk("        seq: %u\n", ntoh16(echo->seq));
        break;
    default:
        printk("     values: 0x%08x\n", ntoh32(hdr->values));
        break;
    }
#ifdef HEXDUMP
    hexdump(stderr, data, len);
#endif
    funlockfile(stderr);
}

/**ICMPの登録*/
void icmp_input(const uint8_t *data, size_t len, ip_addr_t src, ip_addr_t dst,
                struct ip_iface *iface) {
    struct icmp_hdr *hdr;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];

    /**
     * @brief ICMPメッセージの検証を行う
     */
    /**長さ確認*/
    if(len < sizeof(*hdr)) {
        errorf("too short");
        return;
    }
    hdr = (struct icmp_hdr *)data;
    /**checksum確認*/
    if(cksum16((uint16_t *)data, len, 0) != 0) {
        errorf("checksum error, sum=0x%04x, verify=0x%04x", ntoh16(hdr->sum),
               ntoh16(cksum16((uint16_t *)data, len, -hdr->sum)));
        return;
    }
    debugf("%s => %s, len=%lu", ip_addr_ntop(src, addr1, sizeof(addr1)),
           ip_addr_ntop(dst, addr2, sizeof(addr2)), len);
    switch(hdr->type) {
    /**メッセージタイプがECHOのときの処理*/
    case ICMP_TYPE_ECHO:
        /**送信元は受信したインターフェースのユニキャストアドレスにする*/
        if(dst != iface->unicast) { dst = iface->unicast; }
        icmp_output(ICMP_TYPE_ECHOREPLY, hdr->code, hdr->values,
                    (uint8_t *)(hdr + 1), len - sizeof(*hdr), dst, src);
        break;
    default:
        break;
    }
}

/**ICMPの出力*/
int icmp_output(uint8_t type, uint8_t code, uint32_t values,
                const uint8_t *data, size_t len, ip_addr_t src, ip_addr_t dst) {
    uint8_t buf[ICMP_BUFSIZ];
    struct icmp_hdr *hdr;
    size_t msg_len;
    char addr1[IP_ADDR_STR_LEN];
    char addr2[IP_ADDR_STR_LEN];

    hdr = (struct icmp_hdr *)buf;
    /**各フィールド設定*/
    hdr->type = type;
    hdr->code = code;
    hdr->sum = 0;
    hdr->values = values;
    /**ヘッダ直後にデータをmemcpy()で配置*/
    memcpy(hdr + 1, data, len);
    /**メッセージ全体の長さをmsg_lenに格納*/
    msg_len = sizeof(*hdr) + len;
    /**checksumを計算してsumに格納*/
    hdr->sum = cksum16((uint16_t *)hdr, msg_len, 0);
    debugf("%s => %s, len=%lu", ip_addr_ntop(src, addr1, sizeof(addr1)),
           ip_addr_ntop(dst, addr2, sizeof(addr2)), msg_len);
    return ip_output(IP_PROTOCOL_ICMP, (uint8_t *)hdr, msg_len, src, dst);
}

int icmp_init(void) {
    /**ICMPの入力関数をIPに登録*/
    if(ip_protocol_register(IP_PROTOCOL_ICMP, icmp_input) == -1) {
        errorf("ip_protocol_register() failure");
        return -1;
    }
    return 0;
}