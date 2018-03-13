/**
 * @file    libedio24.h
 * @brief   The E-DIO24 Controller Ethernet Driver
 * @author  Yunhui Fu <yhfudev@gmail.com>
 * @version 1.0
 */
#ifndef _LIBEDIO24_H
#define _LIBEDIO24_H 1

#include <stdlib.h>    /* size_t */
#include <sys/types.h> /* ssize_t pid_t off64_t */
#include <stdint.h> // uint8_t

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#ifndef fsync
#define fsync(fd) _commit(fd)
#endif
#ifndef u_int8_t
#define u_int8_t uint8_t
#define u_int16_t uint16_t
#define u_int32_t uint32_t
#define u_int64_t uint64_t
#endif

#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // inet_ntoa()
#endif

// printf formats
#include <inttypes.h> /* for PRIdPTR PRIiPTR PRIoPTR PRIuPTR PRIxPTR PRIXPTR, SCNdPTR SCNiPTR SCNoPTR SCNuPTR SCNxPTR */
#ifndef PRIuSZ
#ifdef __WIN32__                /* or whatever */
#define PRIiSZ "Id"
#define PRIuSZ "Iu"
#else
#define PRIiSZ "zd"
#define PRIuSZ "zu"
#define PRIxSZ "zx"
#define SCNxSZ "zx"
#endif
#define PRIiOFF PRIx64 /*"lld"*/
#define PRIuOFF PRIx64 /*"llu"*/
#endif // PRIuSZ

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define EDIO24_PKT_LENGTH_MIN 7 /**< the mininal length of a edio24 packet */

#define EDIO24_PORT_DISCOVER 54211
#define EDIO24_PORT_COMMAND  54211

// create a packet
ssize_t edio24_pkt_create_opendev       (uint8_t *buffer, size_t sz_buf, uint32_t connect_code);
ssize_t edio24_pkt_create_discoverydev  (uint8_t *buffer, size_t sz_buf);

ssize_t edio24_pkt_create_cmd_dinr      (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id);

ssize_t edio24_pkt_create_cmd_doutr     (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id);
ssize_t edio24_pkt_create_cmd_doutw     (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint32_t mask, uint32_t value);

ssize_t edio24_pkt_create_cmd_dconfr    (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id);
ssize_t edio24_pkt_create_cmd_dconfw    (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint32_t mask, uint32_t value);

ssize_t edio24_pkt_create_cmd_dcounterr (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id);
ssize_t edio24_pkt_create_cmd_dcounterw (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id);

ssize_t edio24_pkt_create_cmd_reset     (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id);
ssize_t edio24_pkt_create_cmd_firmware  (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id);
ssize_t edio24_pkt_create_cmd_netconf   (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id);
ssize_t edio24_pkt_create_cmd_status    (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id);

ssize_t edio24_pkt_create_cmd_blinkled  (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint8_t value);

ssize_t edio24_pkt_create_cmd_confmemr  (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count);
ssize_t edio24_pkt_create_cmd_confmemw  (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count, uint8_t *buffer_data);

ssize_t edio24_pkt_create_cmd_usermemr  (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count);
ssize_t edio24_pkt_create_cmd_usermemw  (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count, uint8_t *buffer_data);

ssize_t edio24_pkt_create_cmd_setmemr   (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count);
ssize_t edio24_pkt_create_cmd_setmemw   (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count, uint8_t *buffer_data);

ssize_t edio24_pkt_create_cmd_bootmemr  (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count);
ssize_t edio24_pkt_create_cmd_bootmemw  (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count, uint8_t *buffer_data);

//int edio24_pkt_read_hdr_start   (uint8_t *buffer, size_t sz_buf, uint8_t * value);
int edio24_pkt_read_hdr_command  (uint8_t *buffer, size_t sz_buf, uint8_t * value);
//int edio24_pkt_read_hdr_frameid (uint8_t *buffer, size_t sz_buf, uint8_t * value);
int edio24_pkt_read_hdr_count    (uint8_t *buffer, size_t sz_buf, uint16_t * value);

int edio24_pkt_read_ret_doutr    (uint8_t *buffer, size_t sz_buf, uint32_t * value);
int edio24_pkt_read_ret_counterr (uint8_t *buffer, size_t sz_buf, uint32_t * value);
int edio24_pkt_read_ret_status   (uint8_t *buffer, size_t sz_buf, uint32_t * value);
int edio24_pkt_read_ret_netconf  (uint8_t *buffer, size_t sz_buf, struct in_addr network[3]);
int edio24_pkt_read_ret_confmemr (uint8_t *buffer, size_t sz_buf, uint16_t count, uint8_t *buffer_ret);
#define edio24_pkt_read_ret_dinr     edio24_pkt_read_ret_doutr
#define edio24_pkt_read_ret_dconfr   edio24_pkt_read_ret_doutr
#define edio24_pkt_read_ret_usermemr edio24_pkt_read_ret_confmemr
#define edio24_pkt_read_ret_setmemr  edio24_pkt_read_ret_confmemr
#define edio24_pkt_read_ret_bootmemr edio24_pkt_read_ret_confmemr

int edio24_pkt_verify (uint8_t *buffer, size_t sz_buf);

#if defined(USE_EDIO24_SERVER) && (USE_EDIO24_SERVER == 1)
// for the server simulator
ssize_t edio24_pkt_create_ret_doutr  (uint8_t *buffer, size_t sz_buf, uint8_t frame_id, uint32_t value);
ssize_t edio24_pkt_create_ret_doutw  (uint8_t *buffer, size_t sz_buf, uint8_t frame_id);
ssize_t edio24_pkt_create_ret_dconfw (uint8_t *buffer, size_t sz_buf, uint8_t frame_id);

const char * edio24_val2cstr_cmd(uint8_t cmd);
const char * edio24_val2cstr_status(uint8_t status);

int edio24_cli_verify_tcp(uint8_t * buffer_in, size_t sz_in, size_t * sz_processed, size_t * sz_needed_in);
int edio24_svr_process_tcp(char flg_force_fail, uint8_t * buffer_in, size_t sz_in, uint8_t * buffer_out, size_t *sz_out, size_t * sz_processed, size_t * sz_needed_in, size_t * sz_needed_out);

int edio24_cli_verify_udp(uint8_t * buffer_in, size_t sz_in);
int edio24_svr_process_udp(char flg_force_fail, uint8_t * buffer_in, size_t sz_in, uint8_t * buffer_out, size_t *sz_out, size_t * sz_needed_out);
#endif

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /* _LIBEDIO24_H */

/*
    Configuration memory map
|=================================================================|
|    Address   |        Value                                     |
|=================================================================|
| 0x00 - 0x07  | Serial number (not used by firmware)             |
|-----------------------------------------------------------------|
| 0x08 - 0x09  | Reserved                                         |
|-----------------------------------------------------------------|
| 0x0A - 0x0F  | MAC address                                      |
|              |  If all 6 bytes are 0xFF then the firmware will  |
|              |  use the Microchip unique MAC address that       |
|              |  is programmed into the micro.                   |
|=================================================================|


    Settings memory map
|=====================================================================================|
|    Address    |        Value                                        | Default value |
|=====================================================================================|
| 0x000 - 0x001 | Network options:                                    | 0x0000        |
|               |   Bit 0: 0 = DHCP enabled     1 = DHCP disabled     |               |
|               |   Bit 1: 0 = Auto IP enabled  1 = Auto IP disabled  |               |
|               |   Bits 2-15 reserved                                |               |
|-------------------------------------------------------------------------------------|
| 0x002 - 0x005 | Default IP address                                  | 192.168.0.101 |
|-------------------------------------------------------------------------------------|
| 0x006 - 0x009 | Default subnet mask                                 | 255.255.255.0 |
|-------------------------------------------------------------------------------------|
| 0x00A - 0x00D | Default gateway address                             | 192.168.0.1   |
|-------------------------------------------------------------------------------------|
| 0x00E - 0x011 | Reserved                                            |               |
|-------------------------------------------------------------------------------------|
| 0x012 - 0x015 | Connection code, 4 bytes                            | 0x00000000    |
|-------------------------------------------------------------------------------------|
| 0x016         | DOut connection mode.  This determines the DOut     | 0             |
|               | value when the connection status changes.           |               |
|               |   0 = no change                                     |               |
|               |   1 = apply specified tristate / latch values       |               |
|-------------------------------------------------------------------------------------|
| 0x017         | Reserved                                            |               |
|-------------------------------------------------------------------------------------|
| 0x018         | DOut port 0 tristate mask for connection /          | 0xFF          |
|               | disconnection (bits set to 0 are outputs, bits set  |               |
|               | to 1 are no change)                                 |               |
|-------------------------------------------------------------------------------------|
| 0x019         | DOut port 1 tristate mask for connection /          | 0xFF          |
|               | disconnection (bits set to 0 are outputs, bits set  |               |
|               | to 1 are no change)                                 |               |
|-------------------------------------------------------------------------------------|
| 0x01A         | DOut port 2 tristate mask for connection /          | 0xFF          |
|               | disconnection (bits set to 0 are outputs, bits set  |               |
|               | to 1 are no change)                                 |               |
|-------------------------------------------------------------------------------------|
| 0x01B         | Reserved                                            |               |
|-------------------------------------------------------------------------------------|
| 0x01C         | DOut port0 latch value when host is connected       | 0x00          |
|-------------------------------------------------------------------------------------|
| 0x01D         | DOut port1 latch value when host is connected       | 0x00          |
|-------------------------------------------------------------------------------------|
| 0x01D         | DOut port2 latch value when host is connected       | 0x00          |
|-------------------------------------------------------------------------------------|
| 0x01F         | Reserved                                            |               |
|-------------------------------------------------------------------------------------|
| 0x020         | DOut port0 latch value when host is disconnected    | 0x00          |
|-------------------------------------------------------------------------------------|
| 0x021         | DOut port1 latch value when host is disconnected    | 0x00          |
|-------------------------------------------------------------------------------------|
| 0x022         | DOut port2 latch value when host is disconnected    | 0x00          |
|-------------------------------------------------------------------------------------|
| 0x023 - 0x0FF | Reserved                                            |               |
|=====================================================================================|

Note: The settings do not take effect until after device is reset or power cycled.

    User memory map
|=================================================================|
|    Address     |        Value                                   |
|=================================================================|
| 0x000 - 0xEEF  | Available for UL use                           |
|=================================================================|

*/
