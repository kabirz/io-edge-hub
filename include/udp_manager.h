#ifndef UDP_MANAGER_H
#define UDP_MANAGER_H

/* winsock2 必须在 windows.h 之前, 否则 winsock1 冲突 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#define IOEDGE_UDP_PORT        8600   /* 固件配置/UDP升级端口 */
#define IOEDGE_UDP_REPLY_PORT  8601   /* DISCOVER 跨网段回复端口 */
#define IOEDGE_UDP_TIMEOUT_MS  1000   /* 单条命令同步等待超时 */

/* opaque 句柄 */
typedef struct UdpManager UdpManager;

UdpManager *UdpManager_Create(void);
void UdpManager_Destroy(UdpManager *m);
const char *UdpManager_GetLastError(UdpManager *m);

/* --- 升级命令 (0x01-0x03, 小端; 0x04/0x05 同端口) --- */
/* FwStart: 发 [0x01][size LE32][keyhash 32B 可选], 回 [0x01][status].
 * keyhash=NULL 时不带 (兼容旧设备); status: 0=失败 1=成功 2=keyhash 不匹配 */
bool UdpManager_FwStart(UdpManager *m, const char *ip, uint32_t img_size,
                        const uint8_t keyhash[32], uint8_t *out_status);
/* FwData: 发 [0x02][data<=511B], 回 [0x02][offset LE32]. */
bool UdpManager_FwData(UdpManager *m, const char *ip, const uint8_t *data, int len,
                       uint32_t *out_offset);
/* FwEnd: 发 [0x03][test 1B][crc16 LE16], 回 [0x03][result 1B]. */
bool UdpManager_FwEnd(UdpManager *m, const char *ip, uint8_t test, uint16_t crc16,
                      uint8_t *out_result);

/* --- 配置命令 (0x10+, 大端) --- */
bool UdpManager_SetIp(UdpManager *m, const char *ip, uint8_t ip4[4], uint8_t *out_ok);  /* 0x10 */
bool UdpManager_GetNet(UdpManager *m, const char *ip, uint8_t ip4[4],
                       uint8_t *out_slave, uint16_t *out_tcp_port);                      /* 0x11 */
bool UdpManager_SetModbus(UdpManager *m, const char *ip, uint8_t slave_id,
                          uint16_t baud, uint8_t *out_ok);                               /* 0x12 */
bool UdpManager_GetModbus(UdpManager *m, const char *ip, uint8_t *out_slave,
                          uint16_t *out_baud);                                           /* 0x13 */
bool UdpManager_SetCan(UdpManager *m, const char *ip, uint16_t can_id,
                       uint16_t baud_k, uint8_t *out_ok);                                /* 0x16 */
bool UdpManager_GetCan(UdpManager *m, const char *ip, uint16_t *out_can_id,
                       uint16_t *out_baud_k);                                            /* 0x17 */
/* DISCOVER (0x18): 向所有本机网卡子网定向广播发送, 单播+8601 监听回复.
 * out 一次性填所有回复: "io-edge-hub <ip> v0.1.0_xxxxxx" 一行一条, '\n' 分隔.
 * out_cap 为 out 缓冲字节. 返回 true=至少发现 1 台. */
bool UdpManager_Discover(UdpManager *m, char *out, int out_cap, int *out_count);
bool UdpManager_FactoryReset(UdpManager *m, const char *ip, uint8_t *out_ok);            /* 0x19 */
bool UdpManager_GetVersion(UdpManager *m, const char *ip, char *out_ver, int out_cap);   /* 0x04 */
bool UdpManager_Reboot(UdpManager *m, const char *ip);                                   /* 0x05 */

/* CRC16-CCITT (poly 0x1021, init 0x0000), 与 Zephyr crc16_ccitt 对齐 (UDP 升级用). */
uint16_t UdpManager_CRC16_CCITT(const uint8_t *data, size_t len);

#endif /* UDP_MANAGER_H */
