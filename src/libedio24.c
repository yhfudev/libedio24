/**
 * \file    libedio24.c
 * \brief   The E-DIO24 Controller Ethernet Driver
 * \author  Yunhui Fu <yhfudev@gmail.com>
 * \version 1.0
 */

#include <stdio.h>
#include <string.h> // memmove()
#include <unistd.h> // STDERR_FILENO
#include <assert.h>

#include "libedio24.h"

#if DEBUG
#include "hexdump.h"
#else
#define hex_dump_to_fd(fd, fragment, size)
#endif

#ifndef PRIuSZ
#include <inttypes.h> /* for PRIdPTR PRIiPTR PRIoPTR PRIuPTR PRIxPTR PRIXPTR, SCNdPTR SCNiPTR SCNoPTR SCNuPTR SCNxPTR */
#ifdef __WIN32__                /* or whatever */
#define PRIuSZ "Iu"
#else
#define PRIuSZ "zu"
#endif
#endif

// Digital I/O Commands
#define CMD_DIN_R            (0x00)  // Read DIO pins
#define CMD_DOUT_R           (0x02)  // Read DIO latch value
#define CMD_DOUT_W           (0x03)  // Write DIO latch value
#define CMD_DCONF_R          (0x04)  // Read DIO configuration value
#define CMD_DCONF_W          (0x05)  // Write DIO Configuration value
// Counter Commands
#define CMD_COUNTER_R        (0x30)  // Read event counter
#define CMD_COUNTER_W        (0x31)  // Reset event counter
// Memory Commands
#define CMD_CONF_MEM_R       (0x40)  // Read configuration memeory
#define CMD_CONF_MEM_W       (0x41)  // Write configuration memory
#define CMD_USR_MEM_R        (0x42)  // Read user memory
#define CMD_USR_MEM_W        (0x43)  // Write user memory
#define CMD_SET_MEM_R        (0x44)  // Read settings memory
#define CMD_SET_MEM_W        (0x45)  // Write settings memory
#define CMD_BOOT_MEM_R       (0x46)  // Read bootloader memory
#define CMD_BOOT_MEM_W       (0x47)  // Write bootloader memory
// Miscellaneous Commands
#define CMD_BLINKLED         (0x50)  // Blink the LED
#define CMD_RESET            (0x51)  // Reset the device
#define CMD_STATUS           (0x52)  // Read the device status
#define CMD_NETWORK_CONF     (0x54)  // Read device network configuration
#define CMD_FIRMWARE         (0x60)  // Enter bootloader for firmware upgrade

#define MSG_SUCCESS         0   // Command succeeded
#define MSG_ERROR_PROTOCOL  1   // Command failed due to improper protocol
// (number of expected data bytes did not match protocol definition)
#define MSG_ERROR_PARAMETER 2   // Command failed due to invalid parameters
// (the data contents were incorrect)
#define MSG_ERROR_BUSY      3   // Command failed because resource was busy
#define MSG_ERROR_READY     4   // Command failed because the resource was not ready
#define MSG_ERROR_TIMEOUT   5   // Command failed due to a resource timeout
#define MSG_ERROR_OTHER     6   // Command failed due to some other error

#define MSG_HEADER_SIZE     6
#define MSG_CHECKSUM_SIZE   1

#define MSG_INDEX_START      0
#define MSG_INDEX_COMMAND    1
#define MSG_INDEX_FRAME      2
#define MSG_INDEX_STATUS     3
#define MSG_INDEX_COUNT_LOW  4  // The maximum value for count is 1024
#define MSG_INDEX_COUNT_HIGH 5
#define MSG_INDEX_DATA       6

#define MSG_REPLY            (0x80)
#define MSG_START            (0xDB)

/**
 * \brief calculate the checksum of E-DIO24 protocol packet
 * \param buffer:   the buffer contains the packet
 * \param length:   the byte size of the packet
 * \return the checksum value
 */
unsigned char
edio24_pkt_checksum(void *buffer, int length)
{
    int i;
    unsigned char checksum = 0;
    assert (NULL != buffer);

    for (i = 0; i < length; i++) {
        checksum += ((unsigned char*) buffer)[i];
    }
    return checksum;
}

/**
 * \brief fill the buffer with the 'open device' UDP packet
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param connect_code: the connect code
 * \return <0 on fail, >0 the size of packet
 *
 * The command to 'open device'.
 */
ssize_t
edio24_pkt_create_opendev (uint8_t *buffer, size_t sz_buf, uint32_t connect_code)
{
    ssize_t ret = 0;

    if (NULL == buffer) {
        return -1;
    }
    if (sz_buf < 5) {
        return -1;
    }
    ret = 5;
    assert (NULL != buffer);
    buffer[0] = 'C'; //0x43;
    buffer[1] =  connect_code        & 0xFF;
    buffer[2] = (connect_code >>  8) & 0xFF;
    buffer[3] = (connect_code >> 16) & 0xFF;
    buffer[4] = (connect_code >> 24) & 0xFF;

    return ret;
}

/**
 * \brief fill the buffer with the 'discovery device' UDP packet
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param connect_code: the connect code
 * \return <0 on fail, >0 the size of packet
 *
 * The command to 'discovery device'.
 */
ssize_t
edio24_pkt_create_discoverydev (uint8_t *buffer, size_t sz_buf)
{
    ssize_t ret = 0;

    if (NULL == buffer) {
        return -1;
    }
    if (sz_buf < 1) {
        return -1;
    }
    ret = 1;
    assert (NULL != buffer);
    buffer[0] = 'D'; //0x44;

    return ret;
}

/**
 * \brief fill the buffer with the CMD_DIN_R request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \return <0 on fail, >0 the size of packet
 */
ssize_t
edio24_pkt_create_cmd_dinr (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id)
{
    static const int data_count_dinr = 0;

    if (NULL == buffer) {
        return -1;
    }
    if (NULL == frame_id) {
        return -1;
    }
    if (sz_buf < MSG_INDEX_DATA + 1 + data_count_dinr) {
        fprintf(stderr, "edio24 warning: doutr buffer size limited.\n");
        return -1;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_DIN_R;
    buffer[MSG_INDEX_START]          = MSG_START;
    buffer[MSG_INDEX_FRAME]          = *frame_id; // increment frame ID with every send
    buffer[MSG_INDEX_STATUS]         = MSG_SUCCESS;
    buffer[MSG_INDEX_COUNT_LOW]      = (unsigned char) ( data_count_dinr       & 0xFF);
    buffer[MSG_INDEX_COUNT_HIGH]     = (unsigned char) ((data_count_dinr >> 8) & 0xFF);

    buffer[MSG_INDEX_DATA + data_count_dinr] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, MSG_INDEX_DATA + data_count_dinr);

    (*frame_id)++;

    return (MSG_INDEX_DATA + 1 + data_count_dinr);
}

/**
 * \brief fill the buffer with the CMD_DOUT_R request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \return <0 on fail, >0 the size of packet
 */
ssize_t
edio24_pkt_create_cmd_doutr (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_dinr (buffer, sz_buf, frame_id);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_DOUT_R;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_DCONF_R request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \return <0 on fail, >0 the size of packet
 */
ssize_t
edio24_pkt_create_cmd_dconfr (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_dinr (buffer, sz_buf, frame_id);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_DCONF_R;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_COUNTER_R request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \return <0 on fail, >0 the size of packet
 */
ssize_t
edio24_pkt_create_cmd_dcounterr (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_dinr (buffer, sz_buf, frame_id);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_COUNTER_R;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_COUNTER_W request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \return <0 on fail, >0 the size of packet
 *
 * This command resets the event counter.  On a write the counter will be reset to 0
 */
ssize_t
edio24_pkt_create_cmd_dcounterw (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_dinr (buffer, sz_buf, frame_id);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_COUNTER_W;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_RESET request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \return <0 on fail, >0 the size of packet
 *
 * The command resets the device.
 */
ssize_t
edio24_pkt_create_cmd_reset (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_dinr (buffer, sz_buf, frame_id);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_RESET;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_STATUS request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \return <0 on fail, >0 the size of packet
 *
 * The command reads the device status.
 */
ssize_t
edio24_pkt_create_cmd_status (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_dinr (buffer, sz_buf, frame_id);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_STATUS;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_NETWORK_CONF request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \return <0 on fail, >0 the size of packet
 *
 * The command reads the current network configuration.
 */
ssize_t
edio24_pkt_create_cmd_netconf (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_dinr (buffer, sz_buf, frame_id);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_NETWORK_CONF;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_FIRMWARE request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \return <0 on fail, >0 the size of packet
 *
 * This command causes the device to reset and enter the bootloader
 * for a firmware upgrade.  It erases a portion of the program memory so
 * the device must have firmware downloaded through the bootloader before
 * it can be used again.
*/
ssize_t
edio24_pkt_create_cmd_firmware (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id)
{
    static const int data_count_firmware = 2;

    if (NULL == buffer) {
        return -1;
    }
    if (NULL == frame_id) {
        return -1;
    }
    if (sz_buf < MSG_INDEX_DATA + 1 + data_count_firmware) {
        return -1;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_BLINKLED;
    buffer[MSG_INDEX_DATA]           = 0xAD; // key
    buffer[MSG_INDEX_DATA + 1]       = 0xAD; // key
    buffer[MSG_INDEX_START]          = MSG_START;
    buffer[MSG_INDEX_FRAME]          = *frame_id; // increment frame ID with every send
    buffer[MSG_INDEX_STATUS]         = MSG_SUCCESS;
    buffer[MSG_INDEX_COUNT_LOW]      = (unsigned char) ( data_count_firmware       & 0xFF);
    buffer[MSG_INDEX_COUNT_HIGH]     = (unsigned char) ((data_count_firmware >> 8) & 0xFF);

    buffer[MSG_INDEX_DATA + data_count_firmware] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, MSG_INDEX_DATA + data_count_firmware);

    (*frame_id)++;

    return (MSG_INDEX_DATA + 1 + data_count_firmware);
}

/**
 * \brief fill the buffer with the CMD_BLINKLED request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param value:    the content of value
 * \return <0 on fail, >0 the size of packet
 */
ssize_t
edio24_pkt_create_cmd_blinkled (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint8_t value)
{
    static const int data_count_blinkled = 1;

    if (NULL == buffer) {
        return -1;
    }
    if (NULL == frame_id) {
        return -1;
    }
    if (sz_buf < MSG_INDEX_DATA + 1 + data_count_blinkled) {
        return -1;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_BLINKLED;
    buffer[MSG_INDEX_DATA]           =   value       & 0xFF;
    buffer[MSG_INDEX_START]          = MSG_START;
    buffer[MSG_INDEX_FRAME]          = *frame_id; // increment frame ID with every send
    buffer[MSG_INDEX_STATUS]         = MSG_SUCCESS;
    buffer[MSG_INDEX_COUNT_LOW]      = (unsigned char) ( data_count_blinkled       & 0xFF);
    buffer[MSG_INDEX_COUNT_HIGH]     = (unsigned char) ((data_count_blinkled >> 8) & 0xFF);

    buffer[MSG_INDEX_DATA + data_count_blinkled] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, MSG_INDEX_DATA + data_count_blinkled);

    (*frame_id)++;

    return (MSG_INDEX_DATA + 1 + data_count_blinkled);
}

/**
 * \brief fill the buffer with the CMD_CONF_MEM_R request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param address:  the address of the config
 * \param count:    the byte size of the conent, the buffer_ret should be equal or larger than this value
 * \return <0 on fail, >0 the size of packet
 *
 * reads the nonvolatile configuration memory.  The cal memory is 16 bytes (address 0 - 0xf)
 */
ssize_t
edio24_pkt_create_cmd_confmemr (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count)
{
    static const int data_count_confmemr = 4;

    if (NULL == buffer) {
        return -1;
    }
    if (NULL == frame_id) {
        return -1;
    }
    if (sz_buf < MSG_INDEX_DATA + 1 + data_count_confmemr) {
        return -1;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]  = CMD_CONF_MEM_R;
    buffer[MSG_INDEX_DATA]     =  address        & 0xFF;
    buffer[MSG_INDEX_DATA + 1] = (address >>  8) & 0xFF;
    buffer[MSG_INDEX_DATA + 2] =  count          & 0xFF;
    buffer[MSG_INDEX_DATA + 3] = (count >>  8)   & 0xFF;
    buffer[MSG_INDEX_START]          = MSG_START;
    buffer[MSG_INDEX_FRAME]          = *frame_id; // increment frame ID with every send
    buffer[MSG_INDEX_STATUS]         = MSG_SUCCESS;
    buffer[MSG_INDEX_COUNT_LOW]      = (unsigned char) ( data_count_confmemr       & 0xFF);
    buffer[MSG_INDEX_COUNT_HIGH]     = (unsigned char) ((data_count_confmemr >> 8) & 0xFF);

    buffer[MSG_INDEX_DATA + data_count_confmemr] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, MSG_INDEX_DATA + data_count_confmemr);

    (*frame_id)++;

    return (MSG_INDEX_DATA + 1 + data_count_confmemr);
}

/**
 * \brief fill the buffer with the CMD_CONF_MEM_W request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param address:  the start address for writing (0-0xf)
 * \param count:    the byte size of the contents in the buffer_data, the size of buffer_data should be equal or larger than it
 * \param buffer_data: the data to be sent to device
 * \return <0 on fail, >0 the size of packet
 *
 * This command writes the nonvolatile configuration memory.  The
 *   config memory is 16 bytes (address 0 - 0xf) The config memory
 *   should only be written during factory setup and has an additional
 *   lock mechanism to prevent inadvertent writes.  To enable writes
 *   to the config memory, first write the unlock code 0xAA55 to
 *   address 0x10.  Writes to the entire meemory range are then
 *   possible.  Write any other value to address 0x10 to lock the
 *   memory after writing.  The amount of data to be writeen is
 *   inferred from the frame count - 2.
 */
ssize_t
edio24_pkt_create_cmd_confmemw (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count, uint8_t *buffer_data)
{
    int data_count_confmemw = 2;

    if (NULL == buffer) {
        return -1;
    }
    if (NULL == frame_id) {
        return -1;
    }
    if (sz_buf < MSG_INDEX_DATA + 1 + data_count_confmemw) {
        return -1;
    }
    assert (count >= 0);
    data_count_confmemw = 2 + count;

    assert (NULL != buffer);
    if (count > 0) {
        memmove (&buffer[MSG_INDEX_DATA + 2], buffer_data, count); // in case the buffer and buffer_date are the same
    }
    buffer[MSG_INDEX_COMMAND]  = CMD_CONF_MEM_W;
    buffer[MSG_INDEX_DATA]     =  address        & 0xFF;
    buffer[MSG_INDEX_DATA + 1] = (address >>  8) & 0xFF;
    //if (count > 0) memmove (&buffer[MSG_INDEX_DATA + 2], buffer_data, count);
    buffer[MSG_INDEX_START]          = MSG_START;
    buffer[MSG_INDEX_FRAME]          = *frame_id; // increment frame ID with every send
    buffer[MSG_INDEX_STATUS]         = MSG_SUCCESS;
    buffer[MSG_INDEX_COUNT_LOW]      = (unsigned char) ( data_count_confmemw       & 0xFF);
    buffer[MSG_INDEX_COUNT_HIGH]     = (unsigned char) ((data_count_confmemw >> 8) & 0xFF);

    buffer[MSG_INDEX_DATA + data_count_confmemw] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, MSG_INDEX_DATA + data_count_confmemw);

    (*frame_id)++;

    return (MSG_INDEX_DATA + 1 + data_count_confmemw);
}

/**
 * \brief fill the buffer with the CMD_USR_MEM_R request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param address:  the address of the config
 * \param count:    the byte size of the conent, the buffer_ret should be equal or larger than this value
 * \return <0 on fail, >0 the size of packet
 */
ssize_t
edio24_pkt_create_cmd_usermemr (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_confmemr (buffer, sz_buf, frame_id, address, count);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_USR_MEM_R;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_USR_MEM_W request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param address:  the start address for writing (0-0xf)
 * \param count:    the byte size of the conent, the buffer_data should be equal or larger than this value
 * \param buffer_data: the data to be sent to device
 * \return <0 on fail, >0 the size of packet
 *
 * This command writes the nonvolatile user memory.  The user memory
 *   is 3824 bytes (address 0 - 0xeef). The amount of data to be
 *   written is inferred from the frame count - 2.  The maximum that
 *   can be written in one transfer is 1024 bytes.
 */
ssize_t
edio24_pkt_create_cmd_usermemw (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count, uint8_t *buffer_data)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_confmemw (buffer, sz_buf, frame_id, address, count, buffer_data);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_USR_MEM_W;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_SET_MEM_R request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param address:  the start address for reading (0-0xff)
 * \param count:    the number of bytes to read (max 256 due to protocol)
 * \return <0 on fail, >0 the size of packet
 */
ssize_t
edio24_pkt_create_cmd_setmemr (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_confmemr (buffer, sz_buf, frame_id, address, count);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_SET_MEM_R;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_SET_MEM_W request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param address:  the start address for writing (0-0xf)
 * \param count:    the byte size of the conent, the buffer_data should be equal or larger than this value
 * \param buffer_data: the data to be sent to device
 * \return <0 on fail, >0 the size of packet
 *
 * This command writes the nonvolatile settings memory.  The
 *   settings memory is 256 bytes (address 0 - 0xff). The amount of
 *   data to be written is inferred from the frame count - 2.  The
 *   settings will be implemented after a device reset.
 */
ssize_t
edio24_pkt_create_cmd_setmemw (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count, uint8_t *buffer_data)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_confmemw (buffer, sz_buf, frame_id, address, count, buffer_data);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_SET_MEM_W;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_BOOT_MEM_R request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param address:  the start address for reading
 * \param count:    the number of bytes to read (max 1024)
 * \return <0 on fail, >0 the size of packet
 *
 * This command reads the bootloader stored in nonvolatile FLASH
 *   memory.  The bootloader is located in program FLASH memory in two
 *   physical address ranges: 0x1D000000 - 0x1D007FFF for bootloader
 *   code and 0x1FC00000 - 0x1FC01FFF for C startup code and
 *   interrupts.  Reads may be performed at any time.
 */
ssize_t
edio24_pkt_create_cmd_bootmemr (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_confmemr (buffer, sz_buf, frame_id, address, count);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_BOOT_MEM_R;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_BOOT_MEM_W request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param address:  the start address for writing (0-0xf)
 * \param count:    the byte size of the conent, the buffer_data should be equal or larger than this value
 * \param buffer_data: the data to be sent to device
 * \return <0 on fail, >0 the size of packet
 *
 * This command writes the bootloader stored in nonvolatile FLASH
 *   memory.  The bootloader is located in program FLASH memory in two
 *   physical address ranges: 0x1D000000 - 0x1D007FFF for bootloader
 *   code and 0x1FC00000 - 0x1FC01FFF for C startup code and
 *   interrupts.  Writes outside these ranges are ignored.  The
 *   bootloader memory is write protected and must be unlocked in
 *   order to write the memory.  The unlock proceedure is to write the
 *   unlock code 0xAA55 to address 0xFFFFFFFE.  Writes to the entire
 *   memory range are then possible.  Write any other value to address
 *   0xFFFFFFFE to lock the memory after writing.
 *
 *   The FLASH memory must be erased prior to programming.  A bulk
 *   erase is perfomred by writing 0xAA55 to address 0x80000000 after
 *   unlocking the memory for write.  The bulk erase will require
 *   approximately 150ms to complete.  Once the erase is complete, the
 *   memory may be written; however, the device will not be able to
 v   boot unless it has a valid bootloader so the device shold not be
 *   reset until the bootloader is completely written and verified
 *   using readBootloaderMemory_DIO24().
 *
 *   The writes are perfomred on 4-byte boundaries internally and it
 *   is recommended that the output data be sent in the same manner.
 *   The amount of data to be written is inferred frolm the frame
 *   count - 2.  The maximum count value is 1024.
 */
ssize_t
edio24_pkt_create_cmd_bootmemw (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint16_t address, uint16_t count, uint8_t *buffer_data)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_confmemw (buffer, sz_buf, frame_id, address, count, buffer_data);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_BOOT_MEM_W;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

/**
 * \brief fill the buffer with the CMD_DOUT_W request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param mask:     the content of mask
 * \param value:    the content of value
 * \return <0 on fail, >0 the size of packet
 */
ssize_t
edio24_pkt_create_cmd_doutw (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint32_t mask, uint32_t value)
{
    static const int data_count_doutw = 6;

    if (NULL == buffer) {
        return -1;
    }
    if (NULL == frame_id) {
        return -1;
    }
    if (sz_buf < MSG_INDEX_DATA + 1 + data_count_doutw) {
        return -1;
    }

    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]  = CMD_DOUT_W;
    buffer[MSG_INDEX_DATA]     =   mask        & 0xFF;
    buffer[MSG_INDEX_DATA + 1] =  (mask >>  8) & 0xFF;
    buffer[MSG_INDEX_DATA + 2] =  (mask >> 16) & 0xFF;
    buffer[MSG_INDEX_DATA + 3] =  value        & 0xFF;
    buffer[MSG_INDEX_DATA + 4] = (value >>  8) & 0xFF;
    buffer[MSG_INDEX_DATA + 5] = (value >> 16) & 0xFF;
    buffer[MSG_INDEX_START]          = MSG_START;
    buffer[MSG_INDEX_FRAME]          = *frame_id; // increment frame ID with every send
    buffer[MSG_INDEX_STATUS]         = MSG_SUCCESS;
    buffer[MSG_INDEX_COUNT_LOW]      = (unsigned char) ( data_count_doutw       & 0xFF);
    buffer[MSG_INDEX_COUNT_HIGH]     = (unsigned char) ((data_count_doutw >> 8) & 0xFF);

    buffer[MSG_INDEX_DATA + data_count_doutw] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, MSG_INDEX_DATA + data_count_doutw);

    (*frame_id)++;

    return (MSG_INDEX_DATA + 1 + data_count_doutw);
}

/**
 * \brief fill the buffer with the CMD_DCONF_W request packet contents
 * \param buffer:   the buffer to be filled
 * \param sz_buf:   the byte size of the buffer
 * \param frame_id: the pointer to the frame id record
 * \param mask:     the content of mask
 * \param value:    the content of value
 * \return <0 on fail, >0 the size of packet
 */
ssize_t
edio24_pkt_create_cmd_dconfw (uint8_t *buffer, size_t sz_buf, uint8_t * frame_id, uint32_t mask, uint32_t value)
{
    ssize_t ret;

    ret = edio24_pkt_create_cmd_doutw(buffer, sz_buf, frame_id, mask, value);
    if (ret < 0) {
        return ret;
    }
    assert (NULL != buffer);
    buffer[MSG_INDEX_COMMAND]        = CMD_DCONF_W;
    buffer[ret - 1] = (unsigned char) 0xff - edio24_pkt_checksum(buffer, ret - 1);
    return ret;
}

int
edio24_pkt_read_hdr_command (uint8_t *buffer, size_t sz_buf, uint8_t * value)
{
    if (NULL == buffer) {
        return -1;
    }
    if (MSG_INDEX_COMMAND >= sz_buf) {
        return -1;
    }
    if (NULL == value) {
        return -1;
    }
    assert (NULL != buffer);
    *value = buffer[MSG_INDEX_COMMAND];
    return 0;
}

/**
 * \brief retrive the count value form the packet header
 * \param buffer:   the buffer contains the packet
 * \param sz_buf:   the byte size of the packet
 * \param value:    the content of count value
 * \return <0 on fail, >0 the size of packet
 */
int
edio24_pkt_read_hdr_count (uint8_t *buffer, size_t sz_buf, uint16_t * value)
{
    if (NULL == buffer) {
        return -1;
    }
    if (sz_buf < MSG_INDEX_COUNT_HIGH + 1) {
        return -1;
    }
    if (NULL == value) {
        return -1;
    } else {
        uint16_t count = 0;
        assert (NULL != buffer);
        count = (buffer[MSG_INDEX_COUNT_HIGH] & 0xFF) << 8;
        count += (buffer[MSG_INDEX_COUNT_LOW] & 0xFF);
        *value = count;
    }
    return 0;
}

/**
 * \brief retrive the value form the data area of a packet
 * \param buffer:   the buffer contains the packet
 * \param sz_buf:   the byte size of the packet
 * \param off_data: the offset from the start of data area
 * \param bytes: how many bytes for the value, little endian
 * \param value:    the content of value
 * \return <0 on fail, =0 success
 */
int
edio24_pkt_read_value (uint8_t *buffer, size_t sz_buf, size_t off_data, size_t bytes, uint32_t * value)
{
    uint32_t val0;
    uint32_t val;
    int shift;
    int i;

    if (bytes > sizeof(uint32_t)) {
        fprintf(stderr, "edio24 required bytes > 4.\n");
        return -1;
    }
    if (0 != edio24_pkt_verify (buffer, sz_buf)) {
        //fprintf(stderr, "edio24 verify error.\n");
        return -1;
    }
    if (NULL == value) {
        fprintf(stderr, "edio24 Parameter error: value.\n");
        return -1;
    }
    if (MSG_INDEX_DATA + 1 + off_data + bytes > sz_buf) {
        return -1;
    }
    assert (NULL != buffer);
    shift = 0;
    val = 0;
    for (i = 0; i < bytes; i ++) {
        val0 = buffer[MSG_INDEX_DATA + off_data + i] & 0xFF;
        if (shift > 0) {
            val0 <<= shift;
        }
        shift += 8;
        val += val0;
    }
    assert (NULL != value);
    *value = val;
    return 0;
}

/**
 * \brief retrive the value form the RET_STATUS packet
 * \param buffer:   the buffer contains the packet
 * \param sz_buf:   the byte size of the packet
 * \param frame_id: the pointer to the frame id record
 * \param value:    the content of value
 * \return <0 on fail, =0 success
 */
int
edio24_pkt_read_ret_status (uint8_t *buffer, size_t sz_buf, uint32_t * value)
{
    return edio24_pkt_read_value(buffer, sz_buf, 0, 2, value);
}

/**
 * \brief retrive the value form the RET_DOUT_R packet
 * \param buffer:   the buffer contains the packet
 * \param sz_buf:   the byte size of the packet
 * \param frame_id: the pointer to the frame id record
 * \param value:    the content of value
 * \return <0 on fail, =0 success
 */
int
edio24_pkt_read_ret_doutr (uint8_t *buffer, size_t sz_buf, uint32_t * value)
{
    return edio24_pkt_read_value(buffer, sz_buf, 0, 3, value);
}

/**
 * \brief retrive the value form the RET_COUNTER_R packet
 * \param buffer:   the buffer contains the packet
 * \param sz_buf:   the byte size of the packet
 * \param frame_id: the pointer to the frame id record
 * \param value:    the content of value
 * \return <0 on fail, =0 success
 */
int
edio24_pkt_read_ret_counterr (uint8_t *buffer, size_t sz_buf, uint32_t * value)
{
    return edio24_pkt_read_value(buffer, sz_buf, 0, 4, value);
}

/**
 * \brief retrive the value form the RET_NETWORK_CONF packet
 * \param buffer:   the buffer contains the packet
 * \param sz_buf:   the byte size of the packet
 * \param frame_id: the pointer to the frame id record
 * \param network:  ip, mask, gw
 * \return <0 on fail, =0 success
 */
int
edio24_pkt_read_ret_netconf (uint8_t *buffer, size_t sz_buf, struct in_addr network[3])
{
    if (NULL == buffer) {
        return -1;
    }
    if (0 > edio24_pkt_read_value(buffer, sz_buf, 0, 4, (uint32_t *)network)) {
        return -1;
    }
    if (0 > edio24_pkt_read_value(buffer, sz_buf, 4, 4, (uint32_t *)(network + 1))) {
        return -1;
    }
    return edio24_pkt_read_value(buffer, sz_buf, 8, 4, (uint32_t *)(network + 2));
}

/**
 * \brief retrive the value form the CMD_CONF_MEM_R packet
 * \param buffer:   the buffer contains the packet
 * \param sz_buf:   the byte size of the packet
 * \param frame_id: the pointer to the frame id record
 * \param count:    the byte size of the buffer_ret, it should be equal to the 'count' in the buffer in
 * \param buffer_ret: the buffer
 * \return <0 on fail, =0 success
 */
int
edio24_pkt_read_ret_confmemr (uint8_t *buffer, size_t sz_buf, uint16_t count, uint8_t *buffer_ret)
{
    // TODO
    uint16_t cnt;
    if (0 > edio24_pkt_read_hdr_count (buffer, sz_buf, &cnt)) {
        return -1;
    }
    assert (count == cnt);
    if (count > 0) {
        assert (NULL != buffer);
        memmove (buffer_ret, &buffer[MSG_INDEX_DATA], count);
    }
    return 0;
}

/**
 * \brief verify if the packet is correct
 * \param buffer:   the buffer contains the packet
 * \param sz_buf:   the byte size of the packet
 * \return <0 on fail, 0 on OK
 */
int
edio24_pkt_verify (uint8_t *buffer, size_t sz_buf)
{
    uint16_t count;
    if (0 != edio24_pkt_read_hdr_count(buffer, sz_buf, &count)) {
        fprintf(stderr, "Verify error: read count.\n");
        return -1;
    }
    if (MSG_INDEX_DATA + 1 + count > sz_buf) {
        fprintf(stderr, "Verify error: buffer size and count.\n");
        return -1;
    }
    assert (NULL != buffer);
    if (buffer[MSG_INDEX_DATA + count] + edio24_pkt_checksum(buffer, MSG_INDEX_DATA + count) != 0xff) {
        fprintf(stderr, "Verify error: checksum.\n");
        return -1;
    }
    return 0;
}


/**
 * \brief create a respont packet
 * \param buffer_out:   the buffer to be filled
 * \param sz_out:   the byte size of the buffer
 * \param cmd:    the command to be responded
 * \param frame_id: the pointer to the frame id record
 * \param status:  the return status
 * \param data_count_respond:    the byte size of the return data, the sizeof buffer_data should be equal or larger than it
 * \return <0 on fail, >0 the size of packet
 */
ssize_t
edio24_pkt_create_respond (uint8_t * buffer_out, size_t sz_out, uint8_t cmd, uint8_t frame_id, uint8_t status, size_t data_count_respond, uint8_t * buffer_data)
{
    if (NULL == buffer_out) {
        return -1;
    }
    if (sz_out < MSG_INDEX_DATA + 1 + data_count_respond) {
        return -1;
    }

    assert (NULL != buffer_out);
    if (data_count_respond > 0) {
        if (NULL == buffer_data) {
            return -1;
        }
        assert (NULL != buffer_data);
        memmove (&buffer_out[MSG_INDEX_DATA], buffer_data, data_count_respond);
    }
    buffer_out[MSG_INDEX_START]          = MSG_START;
    buffer_out[MSG_INDEX_COMMAND]        = cmd | MSG_REPLY;
    buffer_out[MSG_INDEX_FRAME]          = frame_id;
    buffer_out[MSG_INDEX_STATUS]         = status;
    buffer_out[MSG_INDEX_COUNT_LOW]      = (unsigned char) ( data_count_respond       & 0xFF);
    buffer_out[MSG_INDEX_COUNT_HIGH]     = (unsigned char) ((data_count_respond >> 8) & 0xFF);

    buffer_out[MSG_INDEX_DATA + data_count_respond] = (unsigned char) 0xFF - edio24_pkt_checksum(buffer_out, MSG_INDEX_DATA + data_count_respond);

    return (MSG_INDEX_DATA + 1 + data_count_respond);
}

#if defined(USE_EDIO24_SERVER) && (USE_EDIO24_SERVER == 1)

const char *
edio24_val2cstr_cmd(uint8_t cmd)
{
#define EDIO24_V2S(a) case a: return #a
    switch (cmd) {
        EDIO24_V2S(CMD_DIN_R);
        EDIO24_V2S(CMD_DOUT_R);
        EDIO24_V2S(CMD_DOUT_W);
        EDIO24_V2S(CMD_DCONF_R);
        EDIO24_V2S(CMD_DCONF_W);
        EDIO24_V2S(CMD_COUNTER_R);
        EDIO24_V2S(CMD_COUNTER_W);
        EDIO24_V2S(CMD_CONF_MEM_R);
        EDIO24_V2S(CMD_CONF_MEM_W);
        EDIO24_V2S(CMD_USR_MEM_R);
        EDIO24_V2S(CMD_USR_MEM_W);
        EDIO24_V2S(CMD_SET_MEM_R);
        EDIO24_V2S(CMD_SET_MEM_W);
        EDIO24_V2S(CMD_BOOT_MEM_R);
        EDIO24_V2S(CMD_BOOT_MEM_W);
        EDIO24_V2S(CMD_BLINKLED);
        EDIO24_V2S(CMD_RESET);
        EDIO24_V2S(CMD_STATUS);
        EDIO24_V2S(CMD_NETWORK_CONF);
        EDIO24_V2S(CMD_FIRMWARE);
    }
    return "UNKNOWN_CMD";
#undef EDIO24_V2S
}

const char *
edio24_val2cstr_status(uint8_t status)
{
#define EDIO24_V2S(a) case a: return #a
    switch (status) {
        EDIO24_V2S(MSG_SUCCESS);            // Command succeeded
        EDIO24_V2S(MSG_ERROR_PROTOCOL);     // Command failed due to improper protocol (number of expected data bytes did not match protocol definition)
        EDIO24_V2S(MSG_ERROR_PARAMETER);    // Command failed due to invalid parameters (the data contents were incorrect)
        EDIO24_V2S(MSG_ERROR_BUSY);         // Command failed because resource was busy
        EDIO24_V2S(MSG_ERROR_READY);        // Command failed because the resource was not ready
        EDIO24_V2S(MSG_ERROR_TIMEOUT);      // Command failed due to a resource timeout
        EDIO24_V2S(MSG_ERROR_OTHER);        // Command failed due to some other error
    }
    return "UNKNOWN_STATUS";
#undef EDIO24_V2S
}

/**
 * \brief read and verify the return UDP packet from server
 * \param buffer_in: the buffer contains received packets
 * \param sz_in: the byte size of the received packets
 *
 * \return <0 fatal error, user should kill this connection;
 *         =2 the packet illegal, the caller should check if sz_out > 0 and send back the response in buffer_out
 *         =1 need more data, the byte size need data is stored in sz_needed;
 *         =0 on successs, the variable sz_processed return processed data byte size
 */
int
edio24_cli_verify_udp(uint8_t * buffer_in, size_t sz_in)
{
    uint32_t val;

    if ((NULL == buffer_in) || (sz_in < 64)) {
        //fprintf(stderr,"edio24_cli_verify_udp() error: size(%" PRIuSZ ") != 64\n", sz_in);
        return -1;
    }
    assert (NULL != buffer_in);
    if ('D' != buffer_in[0]) {
        //fprintf(stderr,"edio24_cli_verify_udp() error: header != 'D'\n");
        return -2;
    }
    fprintf(stdout,"  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", buffer_in[1], buffer_in[2], buffer_in[3], buffer_in[4], buffer_in[5], buffer_in[6]);
    val = buffer_in[8]; val <<= 8; val += buffer_in[7];
    fprintf(stdout,"  Product ID: 0x%04X\n", val);
    val = buffer_in[10]; val <<= 8; val += buffer_in[9];
    fprintf(stdout,"  Firmware Version: 0x%04X\n", val);
    fprintf(stdout,"  NetBIOS Name: %s\n", &buffer_in[11]);
    val = buffer_in[28]; val <<= 8; val += buffer_in[27];
    fprintf(stdout,"  Command Port: 0x%04X (%d)\n", val, val);
    val = buffer_in[34]; val <<= 8; val += buffer_in[33];
    fprintf(stdout,"  Status: 0x%04X\n", val);
    fprintf(stdout,"  IPv4: %d.%d.%d.%d\n", buffer_in[35], buffer_in[36], buffer_in[37], buffer_in[38]);
    val = buffer_in[40]; val <<= 8; val += buffer_in[39];
    fprintf(stdout,"  Bootloader Version: 0x%04X\n", val);

    return 0;
}

/**
 * \brief process a received UDP client packet
 * \param flg_force_fail: 1 - force output a fail response message
 * \param buffer_in: the buffer contains received packets
 * \param sz_in: the byte size of the received packets
 * \param buffer_out: the buffer for the content of packet need to send
 * \param sz_out: pass in the size of buffer_out, pass out the byte size of data in the buffer_out need to send
 * \param sz_needed_out: the bytes size of buffer need to extend for current buffer_out, if set, try pass in a more larger buffer_out to continue
 *
 * \return <0 fatal error, user should kill this connection;
 *         =2 the packet illegal, the caller should check if sz_out > 0 and send back the response in buffer_out
 *         =1 need more data, the byte size need data is stored in sz_needed;
 *         =0 on successs, the variable sz_processed return processed data byte size
 */
int
edio24_svr_process_udp(char flg_force_fail, uint8_t * buffer_in, size_t sz_in, uint8_t * buffer_out, size_t *sz_out, size_t * sz_needed_out)
{
    if (NULL == buffer_in) {
        return -1;
    }
    if (NULL == buffer_out) {
        return -1;
    }
    if (NULL == sz_out) {
        return -1;
    }
    if (NULL == sz_needed_out) {
        return -1;
    }
    if (sz_in < 1) {
        return 1;
    }

    assert (NULL != buffer_in);
    if ('C' == buffer_in[0]) {
        if (*sz_out < 2) {
            assert (NULL != sz_needed_out);
            *sz_needed_out = 2 - *sz_out;
            return 1;
        }
        assert (NULL != buffer_out);
        buffer_out[0] = 'C';
        buffer_out[1] = 0;
        if (flg_force_fail) {
            buffer_out[1] = 1;
        }
        assert (NULL != sz_out);
        *sz_out = 2;
        assert (NULL != sz_needed_out);
        *sz_needed_out = 0;
    } else if ('D' == buffer_in[0]) {
        if (flg_force_fail) {
            assert (NULL != sz_needed_out);
            *sz_out = 0;
            *sz_needed_out = 0;
            return 0;
        }
        if (*sz_out < 64) {
            assert (NULL != sz_needed_out);
            *sz_needed_out = 64 - *sz_out;
            return 1;
        }
        *sz_needed_out = 0;

        assert (NULL != buffer_out);
        memset(buffer_out, 0, 64);
        buffer_out[0] = 'D';
        // 6-byte MAC
        memset(buffer_out + 1, 0x01, 6);
        // 2-byte product ID
        memset(buffer_out + 7, 0x02, 2);
        // 2-byte firmware version
        memset(buffer_out + 9, 0x03, 2);
        // 16-byte netbios name
        strcpy((char *)(buffer_out + 11), "E-DIO24-XXXXXX"); //"netbios name");
        // 2-byte command port
        memset(buffer_out + 27, 0x04, 2);
        // 4-byte unknown
        memset(buffer_out + 29, 0x05, 4);
        // 2-byte status
        memset(buffer_out + 33, 0x06, 2);
        // 4-byte remote host IPv4 address
        memset(buffer_out + 35, 0x07, 4);
        // 2-byte bootloader version
        memset(buffer_out + 39, 0x08, 2);

        assert (NULL != sz_out);
        *sz_out = 64;
    }
    return 0;
}

/**
 * \brief read and verify the return packet from server
 * \param buffer_in: the buffer contains received packets
 * \param sz_in: the byte size of the received packets
 * \param sz_processed: the bytes size processed in the buffer_in
 * \param sz_needed_in: the bytes size of data need to append to buffer_in
 *
 * \return <0 fatal error, user should kill this connection;
 *         =2 the packet illegal, the caller should check if sz_out > 0 and send back the response in buffer_out
 *         =1 need more data, the byte size need data is stored in sz_needed;
 *         =0 on successs, the variable sz_processed return processed data byte size
 */
int
edio24_cli_verify_tcp(uint8_t * buffer_in, size_t sz_in, size_t * sz_processed, size_t * sz_needed_in)
{
    uint8_t cmd;
    uint8_t status;
    uint16_t count;

    if (sz_processed) {
        *sz_processed = 0;
    }
    if (sz_needed_in) {
        *sz_needed_in = 0;
    }

    fprintf(stderr,"edio24_cli_verify() begin\n");
    // check the mininal size of packet
    if (NULL == buffer_in) {
        fprintf(stderr, "edio24 warning: buffer NULL.\n");
        return -1;
    }
    if (sz_in < MSG_INDEX_DATA + 1) {
        assert (sz_needed_in);
        *sz_needed_in = MSG_INDEX_DATA + 1 - sz_in;
        fprintf(stderr, "edio24 warning: need more data, size=%" PRIuSZ ".\n", *sz_needed_in);
        return 1;
    }
    // read command
    edio24_pkt_read_hdr_command(buffer_in, sz_in, &cmd);
    cmd = cmd & (~MSG_REPLY);
    // read count
    if (edio24_pkt_read_hdr_count (buffer_in, sz_in, &count) < 0) {
        fprintf(stderr, "edio24 error: read the count value cmd=%s(0x%02X).\n", edio24_val2cstr_cmd(cmd), cmd);
        return -1;
    }
    // check the size of packet
    if (MSG_INDEX_DATA + 1 + count > sz_in) {
        assert (sz_needed_in);
        *sz_needed_in = MSG_INDEX_DATA + 1 + count - sz_in;
        fprintf(stderr, "edio24 warning: need more data 2, cmd=%s(0x%02X), size=%" PRIuSZ ".\n", edio24_val2cstr_cmd(cmd), cmd, *sz_needed_in);
        fprintf(stderr, "edio24 cli the buffer:\n");
        hex_dump_to_fd(STDERR_FILENO, buffer_in, sz_in);
        return 1;
    }

    if (0 != edio24_pkt_verify(buffer_in, sz_in)) {
        //fprintf(stderr, "edio24 error in verify the received packet, cmd=%s(0x%02X).\n", edio24_val2cstr_cmd(cmd), cmd);
        return 2;
    }
    fprintf(stderr, "edio24 cli process block:\n");
    hex_dump_to_fd(STDERR_FILENO, buffer_in, MSG_INDEX_DATA + 1 + count);

    assert (NULL != buffer_in);
    status = buffer_in[MSG_INDEX_STATUS];
    fprintf(stderr, "edio24 info: received %s status: %s(0x%02X)\n", edio24_val2cstr_cmd(cmd), edio24_val2cstr_status(status), status);

    switch (cmd) {
    case CMD_DIN_R:
    case CMD_DCONF_R:
    case CMD_DOUT_R:
    {
        uint32_t val2 = 0;
        edio24_pkt_read_value(buffer_in, sz_in, 0, 3, &val2);
        fprintf(stderr, "edio24 info: received %s  value: 0x%06X\n", edio24_val2cstr_cmd(cmd), val2);
    }
        break;

    case CMD_BLINKLED:
    case CMD_DCONF_W:
    case CMD_DOUT_W:
    case CMD_COUNTER_W:
    case CMD_CONF_MEM_W:
    case CMD_USR_MEM_W:
    case CMD_SET_MEM_W:
    case CMD_BOOT_MEM_W:
        break;
    case CMD_COUNTER_R:
    {
        uint32_t val2;
        edio24_pkt_read_value(buffer_in, sz_in, 0, 4, &val2);
        fprintf(stderr, "edio24 info: received %s  value: 0x%08X\n", edio24_val2cstr_cmd(cmd), val2);
    }
        break;
    case CMD_STATUS:
    {
        uint32_t val2;
        edio24_pkt_read_value(buffer_in, sz_in, 0, 2, &val2);
        fprintf(stderr, "edio24 info: received %s  value: 0x%04X\n", edio24_val2cstr_cmd(cmd), val2);
    }
        break;
    case CMD_NETWORK_CONF:
    {
        struct in_addr net[3];
        char name[20];
        memmove (net, &(buffer_in[MSG_INDEX_DATA]), 12);
        assert (inet_ntoa(net[0]));
        assert (inet_ntoa(net[1]));
        assert (inet_ntoa(net[2]));

        memset(name, 0, sizeof(name));
        inet_ntop(AF_INET, &(net[0]), name, sizeof(name));
        fprintf(stderr, "edio24 info: received %s IPv4: %s\n", edio24_val2cstr_cmd(cmd), name);

        memset(name, 0, sizeof(name));
        inet_ntop(AF_INET, &(net[1]), name, sizeof(name));
        fprintf(stderr, "edio24 info: received %s IPv4: %s\n", edio24_val2cstr_cmd(cmd), name);

        memset(name, 0, sizeof(name));
        inet_ntop(AF_INET, &(net[2]), name, sizeof(name));
        fprintf(stderr, "edio24 info: received %s IPv4: %s\n", edio24_val2cstr_cmd(cmd), name);
    }
        break;
    case CMD_CONF_MEM_R:
    case CMD_USR_MEM_R:
    case CMD_SET_MEM_R:
    case CMD_BOOT_MEM_R:
    {
        fprintf(stderr, "edio24 info: received %s  data:\n", edio24_val2cstr_cmd(cmd));
        hex_dump_to_fd(STDERR_FILENO, &(buffer_in[MSG_INDEX_DATA]), count);
    }
        break;

    default:
        fprintf(stderr, "edio24 error: unsupport command in packet: cmd=%s(0x%02X)\n", edio24_val2cstr_cmd(cmd), cmd);
        return -1;
        break;
    }

    assert (NULL != sz_processed);
    *sz_processed = EDIO24_PKT_LENGTH_MIN + count;
    return 0;
}

/**
 * \brief process a received client packet
 * \param flg_force_fail: 1 - force output a fail response message
 * \param buffer_in: the buffer contains received packets
 * \param sz_in: the byte size of the received packets
 * \param buffer_out: the buffer for the content of packet need to send
 * \param sz_out: pass in the size of buffer_out, pass out the byte size of data in the buffer_out need to send
 * \param sz_processed: the bytes size processed in the buffer_in
 * \param sz_needed_in: the bytes size of data need to append to buffer_in
 * \param sz_needed_out: the bytes size of buffer need to extend for current buffer_out, if set, try pass in a more larger buffer_out to continue
 *
 * \return <0 fatal error, user should kill this connection;
 *         =2 the packet illegal, the caller should check if sz_out > 0 and send back the response in buffer_out;
 *         =1 need more data, the byte size need data is stored in sz_needed;
 *         =0 on successs, the variable sz_processed return processed data byte size;
 */
int
edio24_svr_process_tcp(char flg_force_fail, uint8_t * buffer_in, size_t sz_in,
                   uint8_t * buffer_out, size_t *sz_out,
                   size_t * sz_processed, size_t * sz_needed_in, size_t * sz_needed_out)
{
    int ret = 0;
    size_t i;
    uint8_t cmd;
    uint8_t status = MSG_SUCCESS;
    uint16_t count;
    uint32_t address;
    uint32_t sz_data;
    uint16_t len_data; /**< the byte size for data */

    if (NULL == sz_processed) {
        return -1;
    }
    *sz_processed = 0;
    if (NULL == sz_needed_in) {
        return -1;
    }
    *sz_needed_in = 0;
    if (NULL == sz_needed_out) {
        return -1;
    }
    *sz_needed_out = 0;
    if (NULL == buffer_in) {
        fprintf(stderr, "edio24 warning: buffer NULL.\n");
        return -1;
    }
    // check the mininal size of packet
    if (sz_in < MSG_INDEX_DATA + 1) {
        assert (sz_needed_in);
        *sz_needed_in = MSG_INDEX_DATA + 1 - sz_in;
        fprintf(stderr, "edio24 warning: need more data, size=%" PRIuSZ ".\n", *sz_needed_in);
        return 1;
    }
    // read command
    edio24_pkt_read_hdr_command(buffer_in, sz_in, &cmd);
    // read count
    if (edio24_pkt_read_hdr_count (buffer_in, sz_in, &count) < 0) {
        fprintf(stderr, "edio24 error: read the count value, cmd=%s(0x%02X).\n", edio24_val2cstr_cmd(cmd), cmd);
        return -1;
    }
    // check the size of packet
    if (MSG_INDEX_DATA + 1 + count > sz_in) {
        assert (sz_needed_in);
        *sz_needed_in = MSG_INDEX_DATA + 1 + count - sz_in;
        fprintf(stderr, "edio24 warning: need more data 2, cmd=%s(0x%02X), size=%" PRIuSZ ".\n", edio24_val2cstr_cmd(cmd), cmd, *sz_needed_in);
        return 1;
    }

    if (0 != edio24_pkt_verify(buffer_in, sz_in)) {
        flg_force_fail = 1;
        status = MSG_ERROR_PROTOCOL;
        fprintf(stderr, "edio24 error in verify the received packet\n");
        ret = 2;
    }

    if (flg_force_fail) {
        if (MSG_SUCCESS == status) {
            // it's not from pkg_verify, it's random fail
            status = (rand() % MSG_ERROR_OTHER) + 1;
        }
        // TODO: generate fail message
        //return ret;
    }

    assert (NULL != buffer_in);

    fprintf(stderr, "edio24 svr process block:\n");
    hex_dump_to_fd(STDERR_FILENO, buffer_in, MSG_INDEX_DATA + 1 + count);

    fprintf(stderr, "edio24 info: received %s status: %s(0x%02X)\n", edio24_val2cstr_cmd(cmd), edio24_val2cstr_status(buffer_in[MSG_INDEX_STATUS]), buffer_in[MSG_INDEX_STATUS]);

    len_data = 0;
    switch (cmd) {
    case CMD_DIN_R:
    case CMD_DCONF_R:
    case CMD_DOUT_R:
        len_data = 3;
        break;

    case CMD_DCONF_W:
    case CMD_DOUT_W:
    {
        uint32_t mask = 0;
        uint32_t value = 0;
        edio24_pkt_read_value(buffer_in, sz_in, 0, 3, &mask);
        edio24_pkt_read_value(buffer_in, sz_in, 3, 3, &value);
        fprintf(stderr, "edio24 info: received %s, mask: 0x%06X, value: 0x%06X\n", edio24_val2cstr_cmd(cmd), mask, value);
    }
        break;

    case CMD_COUNTER_W:
    case CMD_CONF_MEM_W:
    case CMD_USR_MEM_W:
    case CMD_SET_MEM_W:
    case CMD_BOOT_MEM_W:
        break;
    case CMD_COUNTER_R:
        len_data = 4;
        break;
    case CMD_STATUS:
        len_data = 2;
        break;
    case CMD_NETWORK_CONF:
        len_data = 12;
        break;
    case CMD_USR_MEM_R:  // 0 - 0x0EEF, sz<=1024
    {
        static int max_address = 0x0EEF;
        edio24_pkt_read_value(buffer_in, sz_in, 0, 2, &address);
        edio24_pkt_read_value(buffer_in, sz_in, 2, 2, &sz_data);
        if ((sz_data < 1) || (sz_data > 1024) || (address > max_address) || (address + sz_data > max_address)) {
            fprintf(stderr, "edio24 error: received %s request range out of range: addr=0x%04X, size=0x%04X\n", edio24_val2cstr_cmd(cmd), address, sz_data);
            status = MSG_ERROR_PARAMETER;
            len_data = 0;
        } else {
            len_data = sz_data;
        }
    }
        break;
    case CMD_CONF_MEM_R: // 0 - 0x0F
    {
        static int max_address = 0x0F;
        edio24_pkt_read_value(buffer_in, sz_in, 0, 2, &address);
        edio24_pkt_read_value(buffer_in, sz_in, 2, 2, &sz_data);
        if ((sz_data < 1) || (sz_data > 1024) || (address > max_address) || (address + sz_data > max_address)) {
            fprintf(stderr, "edio24 error: received %s request range out of range: addr=0x%04X, size=0x%04X\n", edio24_val2cstr_cmd(cmd), address, sz_data);
            status = MSG_ERROR_PARAMETER;
            len_data = 0;
        } else {
            len_data = sz_data;
        }
    }
        break;
    case CMD_SET_MEM_R: // 0 - 0xFF
    {
        static int max_address = 0xFF;
        edio24_pkt_read_value(buffer_in, sz_in, 0, 2, &address);
        edio24_pkt_read_value(buffer_in, sz_in, 2, 2, &sz_data);
        if ((sz_data < 1) || (sz_data > 1024) || (address > max_address) || (address + sz_data > max_address)) {
            fprintf(stderr, "edio24 error: received %s request range out of range: addr=0x%04X, size=0x%04X\n", edio24_val2cstr_cmd(cmd), address, sz_data);
            status = MSG_ERROR_PARAMETER;
            len_data = 0;
        } else {
            len_data = sz_data;
        }
    }
        break;
    case CMD_BOOT_MEM_R:
        edio24_pkt_read_value(buffer_in, sz_in, 2, 2, &sz_data);
        len_data = sz_data;
        break;
    case CMD_BLINKLED:
    {
        uint32_t val2;
        edio24_pkt_read_value(buffer_in, sz_in, 0, 1, &val2);
        fprintf(stderr, "edio24 info: received %s  value: 0x%02X\n", edio24_val2cstr_cmd(cmd), val2);
    }
        break;

    default:
        fprintf(stderr, "edio24 error: unsupport command in packet: cmd=%s(0x%02X)\n", edio24_val2cstr_cmd(cmd), cmd);
        ret = -1;
        break;
    }
    if (ret != 0) {
        return ret;
    }
    assert (NULL != sz_out);
    if (MSG_INDEX_DATA + 1 + len_data > *sz_out) {
        assert (sz_needed_out);
        *sz_needed_out = MSG_INDEX_DATA + 1 + len_data - *sz_out;
        fprintf(stderr, "edio24 warning: need more out buffer, size=%" PRIuSZ ".\n", *sz_needed_out);
        *sz_out = 0;
        return 1;
    }
    assert (MSG_INDEX_DATA + 1 + len_data <= *sz_out);
    assert (NULL != buffer_out);

    assert (MSG_INDEX_DATA + 1 + len_data <= *sz_out);
    // random value
    for (i = 0; i < len_data; i ++) {
        buffer_out[MSG_INDEX_DATA + i] = (1 + i) & 0xFF;
    }
    edio24_pkt_create_respond (buffer_out, sizeof(buffer_out), cmd, buffer_in[MSG_INDEX_FRAME], status, len_data, buffer_out);

    assert (NULL != sz_processed);
    assert (NULL != sz_out);
    *sz_processed = EDIO24_PKT_LENGTH_MIN + count;
    assert (*sz_out >= EDIO24_PKT_LENGTH_MIN + len_data);
    *sz_out = EDIO24_PKT_LENGTH_MIN + len_data;
    return 0;
}
#endif // USE_EDIO24_SERVER


#if defined(CIUT_ENABLED) && (CIUT_ENABLED == 1)
#include <ciut.h>

TEST_CASE( .name="edio24-buffer", .description="test edio24 buffers.", .skip=0 ) {
    uint8_t buffer[20];
    uint8_t frame_id = 0;

    SECTION("test parameters for edio24_pkt_create_opendev") {
        //CIUT_LOG ("null buffer");
        REQUIRE(0 > edio24_pkt_create_opendev(NULL, 0, 0));
        REQUIRE(0 > edio24_pkt_create_opendev(NULL, 5, 0));
        REQUIRE(0 > edio24_pkt_create_opendev(buffer, 0, 0));
        REQUIRE(0 > edio24_pkt_create_opendev(buffer, 1, 0));
        REQUIRE(0 > edio24_pkt_create_opendev(buffer, 3, 0));
        REQUIRE(0 > edio24_pkt_create_opendev(buffer, 4, 0));
        REQUIRE(sizeof(buffer) >= 5);
        REQUIRE(5 == edio24_pkt_create_opendev(buffer, 5, 0));
        REQUIRE(5 == edio24_pkt_create_opendev(buffer, 6, 0));
    }
    SECTION("test parameters for edio24_pkt_create_discoverydev") {
        REQUIRE(0 > edio24_pkt_create_discoverydev(NULL, 0));
        REQUIRE(0 > edio24_pkt_create_discoverydev(NULL, 1));
        REQUIRE(0 > edio24_pkt_create_discoverydev(buffer, 0));
        REQUIRE(sizeof(buffer) >= 1);
        REQUIRE(1 == edio24_pkt_create_discoverydev(buffer, 1));
        REQUIRE(1 == edio24_pkt_create_discoverydev(buffer, 10));
    }
    SECTION("test parameters for edio24_pkt_create_cmd_doutw") {
        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 6);
        REQUIRE(0 > edio24_pkt_create_cmd_doutw(NULL, 0, NULL, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_doutw(buffer, 0, NULL, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_doutw(NULL, MSG_INDEX_DATA + 1 + 6, NULL, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_doutw(NULL, 0, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_doutw(NULL, MSG_INDEX_DATA + 1 + 6, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_doutw(buffer, MSG_INDEX_DATA + 1 + 6, NULL, 0, 0));
        REQUIRE(MSG_INDEX_DATA > 1);
        REQUIRE(0 > edio24_pkt_create_cmd_doutw(buffer, 0, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_doutw(buffer, 1, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_doutw(buffer, MSG_INDEX_DATA + 6, &frame_id, 0, 0));
        REQUIRE(MSG_INDEX_DATA + 1 + 6 == edio24_pkt_create_cmd_doutw(buffer, MSG_INDEX_DATA + 1 + 6, &frame_id, 0, 0));
        REQUIRE(1 == frame_id);
        REQUIRE(MSG_INDEX_DATA + 1 + 6 == edio24_pkt_create_cmd_doutw(buffer, sizeof(buffer), &frame_id, 0, 0));
        REQUIRE(2 == frame_id);
    }
    SECTION("test parameters for edio24_pkt_create_cmd_firmware") {
        frame_id = 0;
        REQUIRE(0 > edio24_pkt_create_cmd_firmware(NULL, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_firmware(buffer, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_firmware(NULL, MSG_INDEX_DATA + 1 + 2, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_firmware(NULL, 0, &frame_id));
        REQUIRE(0 > edio24_pkt_create_cmd_firmware(NULL, MSG_INDEX_DATA + 1 + 2, &frame_id));
        REQUIRE(0 > edio24_pkt_create_cmd_firmware(buffer, MSG_INDEX_DATA + 1 + 2, NULL));
        REQUIRE(MSG_INDEX_DATA > 1);
        REQUIRE(0 > edio24_pkt_create_cmd_firmware(buffer, 0, &frame_id));
        REQUIRE(0 > edio24_pkt_create_cmd_firmware(buffer, 1, &frame_id));
        REQUIRE(0 > edio24_pkt_create_cmd_firmware(buffer, MSG_INDEX_DATA + 2, &frame_id));
        REQUIRE(MSG_INDEX_DATA + 1 + 2 == edio24_pkt_create_cmd_firmware(buffer, MSG_INDEX_DATA + 1 + 2, &frame_id));
        REQUIRE(1 == frame_id);
        REQUIRE(MSG_INDEX_DATA + 1 + 2 == edio24_pkt_create_cmd_firmware(buffer, sizeof(buffer), &frame_id));
        REQUIRE(2 == frame_id);
    }
    SECTION("test parameters for edio24_pkt_create_cmd_blinkled") {
        frame_id = 0;
        REQUIRE(0 > edio24_pkt_create_cmd_blinkled(NULL, 0, NULL, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_blinkled(buffer, 0, NULL, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_blinkled(NULL, MSG_INDEX_DATA + 1 + 1, NULL, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_blinkled(NULL, 0, &frame_id, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_blinkled(NULL, MSG_INDEX_DATA + 1 + 1, &frame_id, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_blinkled(buffer, MSG_INDEX_DATA + 1 + 1, NULL, 0));
        REQUIRE(MSG_INDEX_DATA > 1);
        REQUIRE(0 > edio24_pkt_create_cmd_blinkled(buffer, 0, &frame_id, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_blinkled(buffer, 1, &frame_id, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_blinkled(buffer, MSG_INDEX_DATA + 1, &frame_id, 0));
        REQUIRE(MSG_INDEX_DATA + 1 + 1 == edio24_pkt_create_cmd_blinkled(buffer, MSG_INDEX_DATA + 1 + 1, &frame_id, 0));
        REQUIRE(1 == frame_id);
        REQUIRE(MSG_INDEX_DATA + 1 + 1 == edio24_pkt_create_cmd_blinkled(buffer, sizeof(buffer), &frame_id, 0));
        REQUIRE(2 == frame_id);
    }
}

TEST_CASE( .name="edio24-read", .description="test edio24 buffers for edio24_pkt_read_hdr_xxx.", .skip=0 ) {
    uint8_t buffer[20];
    uint8_t frame_id = 0;
    ssize_t ret;

    SECTION("test parameters for edio24_pkt_read_hdr_command") {
        uint8_t val8 = 0;
        assert (1 == sizeof(val8));

        assert (sizeof(buffer) > MSG_INDEX_COMMAND);
        buffer[MSG_INDEX_COMMAND] = 0x73;
        REQUIRE(0 > edio24_pkt_read_hdr_command (NULL, 0, NULL));
        REQUIRE(0 > edio24_pkt_read_hdr_command (buffer, 0, NULL));
        REQUIRE(0 > edio24_pkt_read_hdr_command (NULL, sizeof(buffer), NULL));
        REQUIRE(0 > edio24_pkt_read_hdr_command (NULL, 0, &val8));
        REQUIRE(0 > edio24_pkt_read_hdr_command (NULL, sizeof(buffer), &val8));
        REQUIRE(0 > edio24_pkt_read_hdr_command (buffer, 0, &val8));
        REQUIRE(0 > edio24_pkt_read_hdr_command (buffer, sizeof(buffer), NULL));
        REQUIRE(0 == edio24_pkt_read_hdr_command (buffer, sizeof(buffer), &val8));
        REQUIRE(0x73 == val8);
    }
    SECTION("test parameters for edio24_pkt_read_hdr_count") {
        uint16_t val16 = 0;
        assert (2 == sizeof(val16));

        assert (sizeof(buffer) > MSG_INDEX_COUNT_HIGH);
        assert (sizeof(buffer) > MSG_INDEX_COUNT_LOW);
        buffer[MSG_INDEX_COUNT_HIGH] = 0x15;
        buffer[MSG_INDEX_COUNT_LOW] = 0x73;
        REQUIRE(0 > edio24_pkt_read_hdr_count (NULL, 0, NULL));
        REQUIRE(0 > edio24_pkt_read_hdr_count (buffer, 0, NULL));
        REQUIRE(0 > edio24_pkt_read_hdr_count (NULL, sizeof(buffer), NULL));
        REQUIRE(0 > edio24_pkt_read_hdr_count (NULL, 0, &val16));
        REQUIRE(0 > edio24_pkt_read_hdr_count (NULL, sizeof(buffer), &val16));
        REQUIRE(0 > edio24_pkt_read_hdr_count (buffer, 0, &val16));
        REQUIRE(0 > edio24_pkt_read_hdr_count (buffer, sizeof(buffer), NULL));
        REQUIRE(0 == edio24_pkt_read_hdr_count (buffer, sizeof(buffer), &val16));
        REQUIRE(0x1573 == val16);
    }
    SECTION("test parameters for edio24_pkt_read_value") {
        uint32_t val32 = 0;
        assert (4 == sizeof(val32));

        frame_id = 0;
        REQUIRE(0 > edio24_pkt_read_value (NULL, 0, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_read_value (buffer, 0, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_read_value (NULL, 0, sizeof(buffer), 0, NULL));
        REQUIRE(0 > edio24_pkt_read_value (NULL, 0, 0, &frame_id, NULL));
        REQUIRE(0 > edio24_pkt_read_value (NULL, 0, 0, 0, &val32));
        REQUIRE(0 > edio24_pkt_read_value (buffer, sizeof(buffer), 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_read_value (buffer, 0, 0, 0, &val32));
        REQUIRE(0 > edio24_pkt_read_value (NULL, sizeof(buffer), 0, 0, &val32));
        REQUIRE(0 > edio24_pkt_read_value (buffer, sizeof(buffer), 0, 0, &val32));
        REQUIRE(0 > edio24_pkt_read_value (buffer, sizeof(buffer), 0, 1, NULL));
        REQUIRE(0 == frame_id);
        frame_id = 0;
        ret = edio24_pkt_create_cmd_doutw(buffer, sizeof(buffer), &frame_id, 0x010203, 0x040506);
        REQUIRE(1 == frame_id);
        REQUIRE(0 == edio24_pkt_read_value (buffer, ret, 0, 1, &val32));
        REQUIRE(0 == edio24_pkt_read_value (buffer, ret, 0, 2, &val32));
        REQUIRE(0 == edio24_pkt_read_value (buffer, ret, 0, 3, &val32));
        REQUIRE(0 == edio24_pkt_read_value (buffer, ret, 0, 4, &val32));

        frame_id = 0;
        ret = edio24_pkt_create_cmd_doutw(buffer, sizeof(buffer), &frame_id, 0x010203, 0x040506);
        REQUIRE(1 == frame_id);
        REQUIRE(0 < ret);
        REQUIRE(0 == edio24_pkt_read_value(buffer, ret, 0, 3, &val32));
        REQUIRE(0x010203 == val32);
        REQUIRE(0 == edio24_pkt_read_value(buffer, ret, 3, 3, &val32));
        REQUIRE(0x040506 == val32);

        memset(buffer, 0, sizeof(buffer));
        frame_id = 0;
        ret = edio24_pkt_create_respond (buffer, sizeof(buffer), CMD_DIN_R, frame_id, MSG_SUCCESS, 2, buffer);
        REQUIRE(0 < ret);
        REQUIRE(0 > edio24_pkt_read_ret_status (NULL,   0,   NULL));
        REQUIRE(0 > edio24_pkt_read_ret_status (buffer, 0,   NULL));
        REQUIRE(0 > edio24_pkt_read_ret_status (NULL,   ret, NULL));
        REQUIRE(0 > edio24_pkt_read_ret_status (NULL,   0,   &val32));
        REQUIRE(0 > edio24_pkt_read_ret_status (buffer, ret, NULL));
        REQUIRE(0 > edio24_pkt_read_ret_status (buffer, 0,   &val32));
        REQUIRE(0 > edio24_pkt_read_ret_status (NULL,   ret, &val32));
        REQUIRE(0 == edio24_pkt_read_ret_status (buffer, ret, &val32));
        REQUIRE(MSG_SUCCESS == val32);

        REQUIRE(0 > edio24_pkt_create_respond (NULL, 0,  0, 0, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_respond (buffer, 0,  0, 0, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_respond (NULL, sizeof(buffer),  0, 0, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_respond (NULL, 0,  0, 0, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_respond (NULL, 0,  0, 0, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_respond (NULL, 0,  0, 0, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_respond (NULL, 0,  0, 0, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_respond (NULL, 0,  0, 0, 0, 0, NULL));

        frame_id = 0;
        buffer[2] = 0x01;
        buffer[1] = 0x02;
        buffer[0] = 0x03;
        ret = edio24_pkt_create_respond (buffer, sizeof(buffer), CMD_DOUT_R, frame_id, MSG_SUCCESS, 3, buffer);
        REQUIRE(MSG_INDEX_DATA + 1 + 3 == ret);
        REQUIRE(0 == edio24_pkt_read_ret_doutr (buffer, sizeof(buffer), &val32));
        REQUIRE(0x010203 == val32);

        frame_id = 0;
        buffer[3] = 0x01;
        buffer[2] = 0x02;
        buffer[1] = 0x03;
        buffer[0] = 0x04;
        ret = edio24_pkt_create_respond (buffer, sizeof(buffer), CMD_DOUT_R, frame_id, MSG_SUCCESS, 4, buffer);
        REQUIRE(MSG_INDEX_DATA + 1 + 4 == ret);
        REQUIRE(0 == edio24_pkt_read_ret_counterr (buffer, sizeof(buffer), &val32));
        REQUIRE(0x01020304 == val32);
    }
    SECTION("test parameters for edio24_pkt_read_ret_netconf") {
        //int edio24_pkt_read_ret_netconf (uint8_t *buffer, size_t sz_buf, struct in_addr network[3])
        struct in_addr network[3];
        int i;

        ret = edio24_pkt_create_respond (buffer, sizeof(buffer), CMD_DOUT_R, frame_id, MSG_SUCCESS, 12, buffer);
        REQUIRE(MSG_INDEX_DATA + 1 + 12 == ret);
        REQUIRE(0 > edio24_pkt_read_ret_netconf (NULL, 0, NULL));
        REQUIRE(0 > edio24_pkt_read_ret_netconf (buffer, 0, NULL));
        REQUIRE(0 > edio24_pkt_read_ret_netconf (NULL, ret, NULL));
        REQUIRE(0 > edio24_pkt_read_ret_netconf (NULL, 0, network));
        REQUIRE(0 > edio24_pkt_read_ret_netconf (NULL, ret, network));
        REQUIRE(0 > edio24_pkt_read_ret_netconf (buffer, 0, network));
        REQUIRE(0 > edio24_pkt_read_ret_netconf (buffer, ret, NULL));

        assert (sizeof(buffer) >= MSG_INDEX_DATA + 1 + sizeof(network));
        frame_id = 0;
        buffer[2] = 0x01;
        buffer[1] = 0x02;
        buffer[0] = 0x03;
        ret = edio24_pkt_create_respond (buffer, sizeof(buffer), CMD_DOUT_R, frame_id, MSG_SUCCESS, 3, buffer);
        REQUIRE(MSG_INDEX_DATA + 1 + 3 == ret);
        REQUIRE(0 == edio24_pkt_verify(buffer, ret));
        REQUIRE(0 > edio24_pkt_read_ret_netconf (buffer, ret, network));

        frame_id = 0;
        i = 0;
        buffer[3] = 0x01;
        buffer[2] = 0x02;
        buffer[1] = 0x03;
        buffer[0] = 0x04;
        ret = edio24_pkt_create_respond (buffer, sizeof(buffer), CMD_DOUT_R, frame_id, MSG_SUCCESS, 4, buffer);
        REQUIRE(MSG_INDEX_DATA + 1 + 4 == ret);
        REQUIRE(0 == edio24_pkt_verify(buffer, ret));
        REQUIRE(0 > edio24_pkt_read_ret_netconf (buffer, ret, network));

        frame_id = 0;
        i = 0;
        buffer[3] = 0x01;
        buffer[2] = 0x02;
        buffer[1] = 0x03;
        buffer[0] = 0x04;
        i += 4;
        buffer[i + 3] = 0x05;
        buffer[i + 2] = 0x06;
        buffer[i + 1] = 0x07;
        buffer[i + 0] = 0x08;
        ret = edio24_pkt_create_respond (buffer, sizeof(buffer), CMD_DOUT_R, frame_id, MSG_SUCCESS, 8, buffer);
        REQUIRE(MSG_INDEX_DATA + 1 + 8 == ret);
        REQUIRE(0 == edio24_pkt_verify(buffer, ret));
        REQUIRE(0 > edio24_pkt_read_ret_netconf (buffer, ret, network));

        frame_id = 0;
        i = 0;
        buffer[3] = 0x01;
        buffer[2] = 0x02;
        buffer[1] = 0x03;
        buffer[0] = 0x04;
        i += 4;
        buffer[i + 3] = 0x05;
        buffer[i + 2] = 0x06;
        buffer[i + 1] = 0x07;
        buffer[i + 0] = 0x08;
        i += 4;
        buffer[i + 3] = 0x09;
        buffer[i + 2] = 0x0a;
        buffer[i + 1] = 0x0b;
        buffer[i + 0] = 0x0c;
        ret = edio24_pkt_create_respond (buffer, sizeof(buffer), CMD_DOUT_R, frame_id, MSG_SUCCESS, 12, buffer);
        REQUIRE(MSG_INDEX_DATA + 1 + 12 == ret);
        REQUIRE(0 == edio24_pkt_verify(buffer, ret));
        REQUIRE(0 == edio24_pkt_read_ret_netconf (buffer, ret, network));
    }
    SECTION("test parameters for edio24_pkt_read_ret_confmemr") {

        //int edio24_pkt_read_ret_confmemr (uint8_t *buffer, size_t sz_buf, uint16_t count, uint8_t *buffer_ret)
        frame_id = 0;
        buffer[2] = 0x01;
        buffer[1] = 0x02;
        buffer[0] = 0x03;
        ret = edio24_pkt_create_respond (buffer, sizeof(buffer), CMD_DOUT_R, frame_id, MSG_SUCCESS, 3, buffer);
        REQUIRE(MSG_INDEX_DATA + 1 + 3 == ret);
        REQUIRE(0 == edio24_pkt_verify(buffer, ret));
        REQUIRE(0 == edio24_pkt_read_ret_confmemr (buffer, ret, 3, buffer));
        REQUIRE(buffer[2] == 0x01);
        REQUIRE(buffer[1] == 0x02);
        REQUIRE(buffer[0] == 0x03);

        //int edio24_pkt_verify (uint8_t *buffer, size_t sz_buf)
        frame_id = 0;
        buffer[2] = 0x01;
        buffer[1] = 0x02;
        buffer[0] = 0x03;
        ret = edio24_pkt_create_respond (buffer, sizeof(buffer), CMD_DOUT_R, frame_id, MSG_SUCCESS, 3, buffer);
        REQUIRE(MSG_INDEX_DATA + 1 + 3 == ret);
        REQUIRE(0 == edio24_pkt_verify(buffer, ret));
        buffer[0] += 1;
        REQUIRE(0 > edio24_pkt_verify(buffer, ret));
    }
    SECTION("test parameters for edio24_val2cstr_xxx") {
        uint32_t val32 = 0;
        assert (4 == sizeof(val32));

        //edio24_val2cstr_cmd
        REQUIRE(0 == strcmp("UNKNOWN_CMD", edio24_val2cstr_cmd(CMD_FIRMWARE + 1)));
#define EDIO24_V2S(a) REQUIRE(0 == strcmp(#a, edio24_val2cstr_cmd(a)))
        EDIO24_V2S(CMD_DIN_R);
        EDIO24_V2S(CMD_DOUT_R);
        EDIO24_V2S(CMD_DOUT_W);
        EDIO24_V2S(CMD_DCONF_R);
        EDIO24_V2S(CMD_DCONF_W);
        EDIO24_V2S(CMD_COUNTER_R);
        EDIO24_V2S(CMD_COUNTER_W);
        EDIO24_V2S(CMD_CONF_MEM_R);
        EDIO24_V2S(CMD_CONF_MEM_W);
        EDIO24_V2S(CMD_USR_MEM_R);
        EDIO24_V2S(CMD_USR_MEM_W);
        EDIO24_V2S(CMD_SET_MEM_R);
        EDIO24_V2S(CMD_SET_MEM_W);
        EDIO24_V2S(CMD_BOOT_MEM_R);
        EDIO24_V2S(CMD_BOOT_MEM_W);
        EDIO24_V2S(CMD_BLINKLED);
        EDIO24_V2S(CMD_RESET);
        EDIO24_V2S(CMD_STATUS);
        EDIO24_V2S(CMD_NETWORK_CONF);
        EDIO24_V2S(CMD_FIRMWARE);
#undef EDIO24_V2S

        // edio24_val2cstr_status
        REQUIRE(0 == strcmp("UNKNOWN_STATUS", edio24_val2cstr_status(MSG_ERROR_OTHER + 1)));
#define EDIO24_V2S(a) REQUIRE(0 == strcmp(#a, edio24_val2cstr_status(a)))
        EDIO24_V2S(MSG_SUCCESS);            // Command succeeded
        EDIO24_V2S(MSG_ERROR_PROTOCOL);     // Command failed due to improper protocol (number of expected data bytes did not match protocol definition)
        EDIO24_V2S(MSG_ERROR_PARAMETER);    // Command failed due to invalid parameters (the data contents were incorrect)
        EDIO24_V2S(MSG_ERROR_BUSY);         // Command failed because resource was busy
        EDIO24_V2S(MSG_ERROR_READY);        // Command failed because the resource was not ready
        EDIO24_V2S(MSG_ERROR_TIMEOUT);      // Command failed due to a resource timeout
        EDIO24_V2S(MSG_ERROR_OTHER);        // Command failed due to some other error
#undef EDIO24_V2S
    }
}

TEST_CASE( .name="edio24-read", .description="test edio24 buffers for edio24_pkt_read_hdr_xxx.", .skip=0 ) {
    uint8_t buffer[80];
    uint8_t frame_id = 0;

    SECTION("test parameters for edio24_cli_verify_xxx") {
        int ret;
        size_t sz_out = 0;
        size_t sz_needed_out = 0;
        uint32_t val32 = 0;
        assert (4 == sizeof(val32));

        REQUIRE(0 > edio24_cli_verify_udp(NULL, 0));
        REQUIRE(0 > edio24_cli_verify_udp(buffer, 0));
        REQUIRE(0 > edio24_cli_verify_udp(NULL, sizeof(buffer)));

        // int edio24_cli_verify_udp(uint8_t * buffer_in, size_t sz_in)
        REQUIRE(1 == edio24_pkt_create_discoverydev(buffer, 1));
        //int edio24_svr_process_udp(char flg_force_fail, uint8_t * buffer_in, size_t sz_in, uint8_t * buffer_out, size_t *sz_out, size_t * sz_needed_out)
        sz_needed_out = 5555;
        sz_out = sizeof(buffer);
        assert (sz_out >= 64);
        ret = edio24_svr_process_udp(0, buffer, 1, buffer, &sz_out, &sz_needed_out);
        REQUIRE(0 == ret);
        REQUIRE(64 == sz_out);
        REQUIRE(0 == edio24_cli_verify_udp(buffer, sz_out));
        REQUIRE(0 > edio24_cli_verify_udp(NULL, 0));
        REQUIRE(0 > edio24_cli_verify_udp(buffer, 0));
        REQUIRE(0 > edio24_cli_verify_udp(NULL, sz_out));

        buffer[0] ++;
        REQUIRE(0 > edio24_cli_verify_udp(buffer, sz_out));
        REQUIRE(0 > edio24_cli_verify_udp(NULL, 0));
        REQUIRE(0 > edio24_cli_verify_udp(buffer, 0));
        REQUIRE(0 > edio24_cli_verify_udp(NULL, sz_out));

        // int edio24_cli_verify_tcp(uint8_t * buffer_in, size_t sz_in, size_t * sz_processed, size_t * sz_needed_in)
        size_t sz_processed;
        size_t sz_needed_in;
        frame_id = 0;
        ret = edio24_pkt_create_cmd_blinkled(buffer, MSG_INDEX_DATA + 1 + 1, &frame_id, 0);
        REQUIRE(MSG_INDEX_DATA + 1 + 1 == ret);
        REQUIRE(1 == frame_id);
        REQUIRE(0 == edio24_cli_verify_tcp(buffer, ret, &sz_processed, &sz_needed_in));
        buffer[0] ++;
        REQUIRE(0 != edio24_cli_verify_tcp(buffer, ret, &sz_processed, &sz_needed_in));

    }
}

TEST_CASE( .name="edio24-buffer2", .description="test edio24 buffers for edio24_pkt_create_cmd_dinr.", .skip=0 ) {
    uint8_t buffer[20];
    uint8_t frame_id = 0;

    SECTION("test parameters for edio24_pkt_create_cmd_dinr") {
        frame_id = 0;
        REQUIRE(0 > edio24_pkt_create_cmd_dinr(NULL, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_dinr(buffer, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_dinr(NULL, MSG_INDEX_DATA + 1, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_dinr(NULL, 0, &frame_id));
        REQUIRE(0 > edio24_pkt_create_cmd_dinr(NULL, MSG_INDEX_DATA + 1, &frame_id));
        REQUIRE(0 > edio24_pkt_create_cmd_dinr(buffer, MSG_INDEX_DATA + 1, NULL));
        REQUIRE(MSG_INDEX_DATA > 1);
        REQUIRE(0 > edio24_pkt_create_cmd_dinr(buffer, 0, &frame_id));
        REQUIRE(0 > edio24_pkt_create_cmd_dinr(buffer, 1, &frame_id));
        REQUIRE(0 > edio24_pkt_create_cmd_dinr(buffer, MSG_INDEX_DATA, &frame_id));
        REQUIRE(MSG_INDEX_DATA + 1 == edio24_pkt_create_cmd_dinr(buffer, MSG_INDEX_DATA + 1, &frame_id));
        REQUIRE(1 == frame_id);
        REQUIRE(MSG_INDEX_DATA + 1 == edio24_pkt_create_cmd_dinr(buffer, sizeof(buffer), &frame_id));
        REQUIRE(2 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 6);
        REQUIRE(0 > edio24_pkt_create_cmd_dconfw(NULL, 0, NULL, 0, 0));
        REQUIRE(MSG_INDEX_DATA + 1 + 6 == edio24_pkt_create_cmd_dconfw(buffer, MSG_INDEX_DATA + 1 + 6, &frame_id, 0, 0));
        REQUIRE(1 == frame_id);
        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 6);
        REQUIRE(0 > edio24_pkt_create_cmd_dconfw(NULL, 0, NULL, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_dconfw(buffer, 0, NULL, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_dconfw(NULL, MSG_INDEX_DATA + 1 + 6, NULL, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_dconfw(NULL, 0, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_dconfw(NULL, MSG_INDEX_DATA + 1 + 6, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_dconfw(buffer, MSG_INDEX_DATA + 1 + 6, NULL, 0, 0));
        REQUIRE(MSG_INDEX_DATA > 1);
        REQUIRE(0 > edio24_pkt_create_cmd_dconfw(buffer, 0, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_dconfw(buffer, 1, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_dconfw(buffer, MSG_INDEX_DATA + 6, &frame_id, 0, 0));
        REQUIRE(MSG_INDEX_DATA + 1 + 6 == edio24_pkt_create_cmd_dconfw(buffer, MSG_INDEX_DATA + 1 + 6, &frame_id, 0, 0));
        REQUIRE(1 == frame_id);
        REQUIRE(MSG_INDEX_DATA + 1 + 6 == edio24_pkt_create_cmd_dconfw(buffer, sizeof(buffer), &frame_id, 0, 0));
        REQUIRE(2 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1);
        REQUIRE(0 > edio24_pkt_create_cmd_doutr(NULL, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 == edio24_pkt_create_cmd_doutr(buffer, MSG_INDEX_DATA + 1, &frame_id));
        REQUIRE(1 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1);
        REQUIRE(0 > edio24_pkt_create_cmd_dconfr(NULL, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 == edio24_pkt_create_cmd_dconfr(buffer, MSG_INDEX_DATA + 1, &frame_id));
        REQUIRE(1 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1);
        REQUIRE(0 > edio24_pkt_create_cmd_dcounterr(NULL, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 == edio24_pkt_create_cmd_dcounterr(buffer, MSG_INDEX_DATA + 1, &frame_id));
        REQUIRE(1 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1);
        REQUIRE(0 > edio24_pkt_create_cmd_dcounterw(NULL, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 == edio24_pkt_create_cmd_dcounterw(buffer, MSG_INDEX_DATA + 1, &frame_id));
        REQUIRE(1 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1);
        REQUIRE(0 > edio24_pkt_create_cmd_reset(NULL, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 == edio24_pkt_create_cmd_reset(buffer, MSG_INDEX_DATA + 1, &frame_id));
        REQUIRE(1 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1);
        REQUIRE(0 > edio24_pkt_create_cmd_status(NULL, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 == edio24_pkt_create_cmd_status(buffer, MSG_INDEX_DATA + 1, &frame_id));
        REQUIRE(1 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1);
        REQUIRE(0 > edio24_pkt_create_cmd_netconf(NULL, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 == edio24_pkt_create_cmd_netconf(buffer, MSG_INDEX_DATA + 1, &frame_id));
        REQUIRE(1 == frame_id);
    }
}

TEST_CASE( .name="edio24-buffer3", .description="test edio24 buffers for edio24_pkt_create_cmd_confmemr.", .skip=0 ) {
    uint8_t buffer[20];
    uint8_t frame_id = 0;

    SECTION("test parameters for edio24_pkt_create_cmd_confmemr") {
        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 4);
        REQUIRE(0 > edio24_pkt_create_cmd_confmemr(NULL, 0, NULL, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemr(buffer, 0, NULL, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemr(NULL, MSG_INDEX_DATA + 1 + 4, NULL, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemr(NULL, 0, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemr(NULL, MSG_INDEX_DATA + 1 + 4, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemr(buffer, MSG_INDEX_DATA + 1 + 4, NULL, 0, 0));
        REQUIRE(MSG_INDEX_DATA > 1);
        REQUIRE(0 > edio24_pkt_create_cmd_confmemr(buffer, 0, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemr(buffer, 1, &frame_id, 0, 0));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemr(buffer, MSG_INDEX_DATA + 4, &frame_id, 0, 0));
        REQUIRE(MSG_INDEX_DATA + 1 + 4 == edio24_pkt_create_cmd_confmemr(buffer, MSG_INDEX_DATA + 1 + 4, &frame_id, 0, 0));
        REQUIRE(1 == frame_id);
        REQUIRE(MSG_INDEX_DATA + 1 + 4 == edio24_pkt_create_cmd_confmemr(buffer, sizeof(buffer), &frame_id, 0, 0));
        REQUIRE(2 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 4);
        REQUIRE(0 > edio24_pkt_create_cmd_usermemr(NULL, 0, NULL, 0, 0));
        REQUIRE(MSG_INDEX_DATA + 1 + 4 == edio24_pkt_create_cmd_usermemr(buffer, MSG_INDEX_DATA + 1 + 4, &frame_id, 0, 0));
        REQUIRE(1 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 4);
        REQUIRE(0 > edio24_pkt_create_cmd_setmemr(NULL, 0, NULL, 0, 0));
        REQUIRE(MSG_INDEX_DATA + 1 + 4 == edio24_pkt_create_cmd_setmemr(buffer, MSG_INDEX_DATA + 1 + 4, &frame_id, 0, 0));
        REQUIRE(1 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 4);
        REQUIRE(0 > edio24_pkt_create_cmd_bootmemr(NULL, 0, NULL, 0, 0));
        REQUIRE(MSG_INDEX_DATA + 1 + 4 == edio24_pkt_create_cmd_bootmemr(buffer, MSG_INDEX_DATA + 1 + 4, &frame_id, 0, 0));
        REQUIRE(1 == frame_id);
    }
}

TEST_CASE( .name="edio24-buffer4", .description="test edio24 buffers for edio24_pkt_create_cmd_confmemw.", .skip=0 ) {
    uint8_t buffer[20];
    uint8_t frame_id = 0;

    SECTION("test parameters for edio24_pkt_create_cmd_confmemw") {
        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 2);
        REQUIRE(0 > edio24_pkt_create_cmd_confmemw(NULL, 0, NULL, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemw(buffer, 0, NULL, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemw(NULL, MSG_INDEX_DATA + 1 + 2, NULL, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemw(NULL, 0, &frame_id, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemw(NULL, MSG_INDEX_DATA + 1 + 2, &frame_id, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemw(buffer, MSG_INDEX_DATA + 1 + 2, NULL, 0, 0, NULL));
        REQUIRE(MSG_INDEX_DATA > 1);
        REQUIRE(0 > edio24_pkt_create_cmd_confmemw(buffer, 0, &frame_id, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemw(buffer, 1, &frame_id, 0, 0, NULL));
        REQUIRE(0 > edio24_pkt_create_cmd_confmemw(buffer, MSG_INDEX_DATA + 2, &frame_id, 0, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 + 2 == edio24_pkt_create_cmd_confmemw(buffer, MSG_INDEX_DATA + 1 + 2, &frame_id, 0, 0, NULL));
        REQUIRE(1 == frame_id);
        REQUIRE(MSG_INDEX_DATA + 1 + 2 == edio24_pkt_create_cmd_confmemw(buffer, sizeof(buffer), &frame_id, 0, 0, NULL));
        REQUIRE(2 == frame_id);
        REQUIRE(MSG_INDEX_DATA + 1 + 2 + 1 == edio24_pkt_create_cmd_confmemw(buffer, sizeof(buffer), &frame_id, 0, 1, buffer));
        REQUIRE(3 == frame_id);
        REQUIRE(MSG_INDEX_DATA + 1 + 2 + 2 == edio24_pkt_create_cmd_confmemw(buffer, sizeof(buffer), &frame_id, 0, 2, buffer));
        REQUIRE(4 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 2);
        REQUIRE(0 > edio24_pkt_create_cmd_usermemw(NULL, 0, NULL, 0, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 + 2 == edio24_pkt_create_cmd_usermemw(buffer, MSG_INDEX_DATA + 1 + 2, &frame_id, 0, 0, NULL));
        REQUIRE(1 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 2);
        REQUIRE(0 > edio24_pkt_create_cmd_setmemw(NULL, 0, NULL, 0, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 + 2 == edio24_pkt_create_cmd_setmemw(buffer, MSG_INDEX_DATA + 1 + 2, &frame_id, 0, 0, NULL));
        REQUIRE(1 == frame_id);

        frame_id = 0;
        REQUIRE(sizeof(buffer) >= MSG_INDEX_DATA + 1 + 2);
        REQUIRE(0 > edio24_pkt_create_cmd_bootmemw(NULL, 0, NULL, 0, 0, NULL));
        REQUIRE(MSG_INDEX_DATA + 1 + 2 == edio24_pkt_create_cmd_bootmemw(buffer, MSG_INDEX_DATA + 1 + 2, &frame_id, 0, 0, NULL));
        REQUIRE(1 == frame_id);
    }
}

TEST_CASE( .name="edio24-svr-process", .description="test edio24_svr_process_xxx.", .skip=0 ) {
    uint8_t buffer[100];
    int ret;

    SECTION("test parameters") {
        size_t sz_needed_out;
        size_t sz_out;
        //CIUT_LOG ("null buffer");
        // int edio24_svr_process_tcp(char flg_force_fail, uint8_t * buffer_in, size_t sz_in, uint8_t * buffer_out, size_t *sz_out, size_t * sz_processed, size_t * sz_needed_in, size_t * sz_needed_out)
        // int edio24_svr_process_udp(char flg_force_fail, uint8_t * buffer_in, size_t sz_in, uint8_t * buffer_out, size_t *sz_out, size_t * sz_needed_out)
        REQUIRE(0 > edio24_svr_process_tcp(0, NULL, 0, NULL, NULL, NULL, NULL, NULL));
        REQUIRE(0 > edio24_svr_process_udp(0, NULL, 0, NULL, NULL, NULL));

        REQUIRE(0 > edio24_svr_process_tcp(0, buffer, 0, NULL, NULL, NULL, NULL, NULL));
        REQUIRE(0 > edio24_svr_process_udp(0, buffer, 0, NULL, NULL, NULL));

        REQUIRE(0 > edio24_svr_process_tcp(0, buffer, 0, NULL, NULL, NULL, NULL, NULL));
        sz_out = sizeof(buffer);
        sz_needed_out = 0;
        REQUIRE(0 > edio24_svr_process_udp(0, buffer, 0, buffer, &sz_out, NULL));
        REQUIRE(0 != edio24_svr_process_udp(0, buffer, 0, buffer, &sz_out, &sz_needed_out));
        sz_out = 0;
        REQUIRE(0 != edio24_svr_process_udp(0, buffer, 0, buffer, &sz_out, &sz_needed_out));

        ret = edio24_pkt_create_opendev(buffer, sizeof(buffer), 0x1A);
        REQUIRE(0 < ret);
        REQUIRE(0 > edio24_svr_process_udp(0, buffer, ret, NULL, NULL, NULL));
        REQUIRE(0 > edio24_svr_process_udp(1, buffer, ret, NULL, NULL, NULL));
        REQUIRE(0 > edio24_svr_process_udp(0, buffer, ret, buffer, NULL, NULL));
        sz_out = 0;
        REQUIRE(0 > edio24_svr_process_udp(0, buffer, ret, buffer, &sz_out, NULL));
        REQUIRE(0 > edio24_svr_process_udp(1, buffer, ret, buffer, &sz_out, NULL));
        sz_out = sizeof(buffer);
        REQUIRE(0 > edio24_svr_process_udp(0, buffer, ret, buffer, &sz_out, NULL));
        REQUIRE(0 > edio24_svr_process_udp(1, buffer, ret, buffer, &sz_out, NULL));

        sz_out = 0;
        sz_needed_out = 5555;
        REQUIRE(1 == edio24_svr_process_udp(0, buffer, ret, buffer, &sz_out, &sz_needed_out));
        CIUT_LOG ("sz_out=%" PRIuSZ "; sz_needed_out=%" PRIuSZ ";", sz_out, sz_needed_out);
        REQUIRE(0 == sz_out);
        REQUIRE(2 == sz_needed_out);

        sz_out = sizeof(buffer);
        sz_needed_out = 5555;
        REQUIRE(0 == edio24_svr_process_udp(0, buffer, ret, buffer, &sz_out, &sz_needed_out));
        REQUIRE(2 == sz_out);
        REQUIRE(0 == sz_needed_out);

        sz_out = sizeof(buffer);
        sz_needed_out = 5555;
        REQUIRE(0 == edio24_svr_process_udp(1, buffer, ret, buffer, &sz_out, &sz_needed_out));
        REQUIRE(2 == sz_out);
        REQUIRE(0 == sz_needed_out);

        ret = edio24_pkt_create_discoverydev(buffer, sizeof(buffer));
        REQUIRE(0 < ret);
        REQUIRE(0 > edio24_svr_process_udp(0, buffer, 0, NULL, NULL, NULL));
        sz_out = sizeof(buffer);
        assert (sz_out >= 64);
        REQUIRE(0 > edio24_svr_process_udp(0, buffer, ret, buffer, &sz_out, NULL));
        sz_out = 0;
        REQUIRE(0 != edio24_svr_process_udp(0, buffer, ret, buffer, &sz_out, &sz_needed_out));
        REQUIRE(64 == sz_needed_out);

        sz_out = sizeof(buffer);
        sz_needed_out = 5555;
        REQUIRE(0 == edio24_svr_process_udp(0, buffer, ret, buffer, &sz_out, &sz_needed_out));
        CIUT_LOG ("sz_out=%" PRIuSZ "; sz_needed_out=%" PRIuSZ ";", sz_out, sz_needed_out);
        REQUIRE(64 == sz_out);
        REQUIRE(0 == sz_needed_out);
        REQUIRE(0 == edio24_svr_process_udp(1, buffer, ret, buffer, &sz_out, &sz_needed_out));
        REQUIRE(0 == sz_needed_out);
        REQUIRE(0 == sz_out);
    }
}

#endif /* CIUT_ENABLED */

//#define CIUT_PLACE_MAIN 1 /**< user defined, a local macro defined to 1 to place main() inside a c file, use once */
//#include <ciut.h>
