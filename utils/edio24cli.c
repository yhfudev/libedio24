/**
 * \file    edio24cli.c
 * \brief   a E-DIO24 client
 * \author  Yunhui Fu <yhfudev@gmail.com>
 * \version 1.0
 */

#define EDIO24CLI_MAIN  1
#define EDIO24CLI_MINOR 0

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // memmove()
#include <unistd.h> // STDERR_FILENO, usleep()
#include <libgen.h> // basename()
#include <getopt.h>

#include <assert.h>

#include <uv.h>

#include "libedio24.h"
#include "utils.h"

#if DEBUG
#include "hexdump.h"
#else
#define hex_dump_to_fd(fd, fragment, size)
#endif

#ifndef NUM_ARRAY
#define NUM_ARRAY(a) (sizeof(a)/sizeof(a[0]))
#endif

uv_loop_t * loop = NULL; /**< this have to be global variable, since it needs to access in on_xxxx() when service new connections */

typedef struct _edio24cli_t {
    uint8_t frame; /**< the sequence number */

    const char * fn_conf; /**< the file name of execute file */

    struct sockaddr_in addr_tcp;  /**< thep socket addr for commands (TCP) */
    uv_tcp_t uvtcp;
    uv_connect_t connect;
    // TODO: a list of remote commands load from file?
    size_t num_requests; /**< the total number of requests sent */
    size_t num_responds; /**< the total number of responds received */
    time_t starttime;
    time_t timeout;

    size_t sz_data; /**< the lenght of data in the buffer */
    uint8_t buffer[EDIO24_PKT_LENGTH_MIN + 1024]; /**< the buffer to cache the received packets */
} edio24cli_t;

edio24cli_t g_edio24cli;

typedef struct {
    uv_udp_send_t req; // should be the first item to bring by the argument

    // the other items bring by the 'req'
    uv_buf_t buf;
} udp_send_buf_t;

void
udp_send_buf_free (udp_send_buf_t *wr)
{
    free(wr->buf.base);
    free(wr);
}

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_buf_t;

void
write_buf_free (write_buf_t *wr)
{
    free(wr->buf.base);
    free(wr);
}

void
alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

/*****************************************************************************/
/**
 * \brief process response packet in the buffer of edio24cli_t
 * \param ped: the edio24cli with buffer
 * \param stream: the libuv socket
 *
 * \return 0 on successs
 *
 * process the data in the buffer, until there's no more data can be processed in one round
 */
ssize_t
edio24cli_process_data (edio24cli_t * ped, uv_stream_t *stream)
{
    uint8_t * buffer_in = NULL;
    size_t sz_in;
    size_t sz_processed;
    size_t sz_needed_in;

    //int flg_again = 0;
    int ret;

    assert (NULL != ped);
    assert (NULL != stream);

    fprintf(stderr,"tcp cli edio24cli_process_data() BEGIN\n");
    while(1) { // while (ped->sz_data > 0) {
        buffer_in = ped->buffer;
        sz_in = ped->sz_data;
        //flg_again = 0;
        sz_processed = 0;
        sz_needed_in = 0;

        fprintf(stderr,"tcp cli edio24cli_process_data() ped->sz_data=%" PRIuSZ "\n", ped->sz_data);
        assert (NULL != buffer_in);
        assert (sz_in >= 0);
        fprintf(stderr,"tcp cli edio24cli_process_data() call edio24_cli_verify\n");
        ret = edio24_cli_verify_tcp(buffer_in, sz_in, &sz_processed, &sz_needed_in);
        if (sz_processed > 0) {
            // remove the head of data of size sz_processed
            size_t sz_rest;
            assert (sz_processed <= ped->sz_data);
            sz_rest = ped->sz_data - sz_processed;
            if (sz_rest > 0) {
                memmove (ped->buffer, &(ped->buffer[sz_processed]), sz_rest);
            }
            ped->sz_data = sz_rest;
        }
        if (sz_needed_in > 0) {
            fprintf(stderr, "need more data: %" PRIuSZ "\n", sz_needed_in);
            break;
        }
        if (ret < 0) {
            break;
        } else if (ret == 0) {
            //flg_again = 1;
            g_edio24cli.num_responds ++;
        } else if (ret == 1) {
            //flg_again = 1;
        } else if (ret == 2) {
            break;
        }
    }
    return 0;
}

/*****************************************************************************/
void
on_tcp_cli_close(uv_handle_t* handle)
{
    fprintf(stderr, "tcp cli closed.\n");
}

void
on_tcp_cli_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    if(nread > 0) {
        // push the content to a center buffer for this TCP connection
        // and fetch the cmd to fetch appriate length of data as a packet
        // to process
        size_t sz_copy;
        size_t sz_processed;

        fprintf(stderr,"tcp cli read block, size=%" PRIiSZ ":\n", nread);
        hex_dump_to_fd(STDERR_FILENO, (opaque_t *)(buf->base), nread);

        fprintf(stderr, "tcp cli process data 1\n");
        edio24cli_process_data (&g_edio24cli, stream);
        sz_processed = 0;
        while (sz_processed < nread) {
            sz_copy = nread - sz_processed;
            assert (sizeof(g_edio24cli.buffer) >= g_edio24cli.sz_data);
            if (sz_copy + g_edio24cli.sz_data > sizeof(g_edio24cli.buffer)) {
                sz_copy = sizeof(g_edio24cli.buffer) - g_edio24cli.sz_data;
            }
            fprintf(stderr, "tcp cli push received data size=%" PRIuSZ ", processed=%" PRIuSZ "\n", sz_copy, sz_processed);
            if (sz_copy > 0) {
                memmove (g_edio24cli.buffer + g_edio24cli.sz_data, buf->base + sz_processed, sz_copy);
                sz_processed += sz_copy;
                g_edio24cli.sz_data += sz_copy;
            } else {
                // error? full?
                fprintf(stderr, "tcp cli no more received data to be push\n");
                break;
            }
            fprintf(stderr, "tcp cli process data 2\n");
            edio24cli_process_data (&g_edio24cli, stream);
        }
        if (sz_processed < nread) {
            // we're stalled here, because the content can't be processed by the function edio24cli_process_data()
            // error
            fprintf(stderr, "tcp cli data stalled\n");
            //uv_close((uv_handle_t *)stream, on_tcp_cli_close);
        }
    }
    if (nread == 0) {
        fprintf(stderr,"tcp cli read zero!\n");
    }
    if (nread < 0) {
        //we got an EOF
        fprintf(stderr,"tcp cli read EOF!\n");
        uv_close((uv_handle_t*)stream, on_tcp_cli_close);
    }

    free(buf->base);
    if (g_edio24cli.num_responds >= g_edio24cli.num_requests) {
        fprintf(stderr,"tcp cli received responses(%" PRIuSZ ") exceed requests(%" PRIuSZ ")!\n", g_edio24cli.num_responds, g_edio24cli.num_requests);
        uv_close((uv_handle_t*)stream, on_tcp_cli_close);
        raise(SIGINT); // send signal and handle by uv_signal_cb
    }
}

void
on_tcp_cli_write_end(uv_write_t* req, int status)
{
    if (status) {
        fprintf(stderr, "tcp cli write error %s.\n", uv_strerror(status));
        uv_close(req->handle, on_tcp_cli_close);
        return;
    } else {
        fprintf(stderr, "tcp cli write successfull.\n");
    }
    write_buf_free(req);
}

void
send_buffer(uv_stream_t* stream, uint8_t *buffer1, size_t szbuf)
{
    int r;
    write_buf_t * wbuf = NULL;

    wbuf = malloc (sizeof(*wbuf));
    assert (NULL != wbuf);
    memset(wbuf, 0, sizeof(*wbuf));
    alloc_buffer(NULL, szbuf, &(wbuf->buf));
    memmove (wbuf->buf.base, buffer1, szbuf);
    assert (NULL != wbuf->buf.base);
    assert (wbuf->buf.len >= szbuf);
    r = uv_write(wbuf, stream, &(wbuf->buf), 1, on_tcp_cli_write_end);
    if (r) {
        /* error */
        fprintf(stderr, "tcp cli error in write() %s\n", uv_strerror(r));
    }
}

#define STRCMP_STATIC(buf, static_str) strncmp(buf, static_str, sizeof(static_str)-1)

/**
 * \brief parse the lines in the buffer and send out packets base on the command
 * \param pos: the position in the file
 * \param buf: the buffer
 * \param size: the size of buffer
 * \param userdata: the pointer passed by the user
 *
 * \return 0 on successs, <0 on error
 *
 * TODO: add command 'sleep' to support delay between commands.
 */
int
process_command(off_t pos, char * buf, size_t size, void *userdata)
{
    uv_stream_t* stream = (uv_stream_t *)userdata;
    ssize_t ret = -1;
    uint8_t buffer1[100];
    uint8_t buffer2[100];
    uint32_t address = 0xFF;
    ssize_t count = sizeof(buffer2);
    char * endptr = NULL;

    fprintf(stderr, "edio24cli process line: '%s'\n", buf);

    if (0 == STRCMP_STATIC (buf, "DOutW")) {
        uint32_t mask = 0xFF;
        uint32_t value = 0;
        char * endptr = NULL;
        // long int strtol(const char *str, char **endptr, int base);
        mask = strtol(buf + 6, &endptr, 16);
        value = strtol(endptr + 1, &endptr, 16);
        ret = edio24_pkt_create_cmd_doutw (buffer1, sizeof(buffer1), &(g_edio24cli.frame), mask, value);

    } else if (0 == STRCMP_STATIC (buf, "DConfigW")) {
        uint32_t mask = 0xFF;
        uint32_t value = 0;
        mask = strtol(buf + 9, &endptr, 16);
        value = strtol(endptr + 1, &endptr, 16);
        ret = edio24_pkt_create_cmd_dconfw (buffer1, sizeof(buffer1), &(g_edio24cli.frame), mask, value);

    } else if (0 == STRCMP_STATIC (buf, "DIn")) {
        ret = edio24_pkt_create_cmd_dinr (buffer1, sizeof(buffer1), &(g_edio24cli.frame));

    } else if (0 == STRCMP_STATIC (buf, "DOutR")) {
        ret = edio24_pkt_create_cmd_doutr (buffer1, sizeof(buffer1), &(g_edio24cli.frame));

    } else if (0 == STRCMP_STATIC (buf, "DConfigR")) {
        ret = edio24_pkt_create_cmd_dconfr (buffer1, sizeof(buffer1), &(g_edio24cli.frame));

    } else if (0 == STRCMP_STATIC (buf, "CounterR")) {
        ret = edio24_pkt_create_cmd_dcounterr (buffer1, sizeof(buffer1), &(g_edio24cli.frame));

    } else if (0 == STRCMP_STATIC (buf, "CounterW")) {
        ret = edio24_pkt_create_cmd_dcounterw (buffer1, sizeof(buffer1), &(g_edio24cli.frame));

    } else if (0 == STRCMP_STATIC (buf, "BlinkLED")) {
        address = strtol(buf + 9, &endptr, 16);
        ret = edio24_pkt_create_cmd_blinkled (buffer1, sizeof(buffer1), &(g_edio24cli.frame), address);

    } else if (0 == STRCMP_STATIC (buf, "Reset")) {
        ret = edio24_pkt_create_cmd_status (buffer1, sizeof(buffer1), &(g_edio24cli.frame));

    } else if (0 == STRCMP_STATIC (buf, "Status")) {
        ret = edio24_pkt_create_cmd_status (buffer1, sizeof(buffer1), &(g_edio24cli.frame));

    } else if (0 == STRCMP_STATIC (buf, "NetworkConfig")) {
        ret = edio24_pkt_create_cmd_netconf (buffer1, sizeof(buffer1), &(g_edio24cli.frame));

    } else if (0 == STRCMP_STATIC (buf, "FirmwareUpgrade")) {
        ret = edio24_pkt_create_cmd_firmware (buffer1, sizeof(buffer1), &(g_edio24cli.frame));

    } else if (0 == STRCMP_STATIC (buf, "BootloaderMemoryR")) {
        address = strtol(buf + 14, &endptr, 16);
        count = strtol(endptr + 1, &endptr, 16);
        ret = edio24_pkt_create_cmd_bootmemr (buffer1, sizeof(buffer1), &(g_edio24cli.frame), address, count);
    } else if (0 == STRCMP_STATIC (buf, "SettingsMemoryR")) {
        address = strtol(buf + 14, &endptr, 16);
        count = strtol(endptr + 1, &endptr, 16);
        ret = edio24_pkt_create_cmd_setmemr (buffer1, sizeof(buffer1), &(g_edio24cli.frame), address, count);
    } else if (0 == STRCMP_STATIC (buf, "ConfigMemoryR")) {
        address = strtol(buf + 14, &endptr, 16);
        count = strtol(endptr + 1, &endptr, 16);
        ret = edio24_pkt_create_cmd_confmemr (buffer1, sizeof(buffer1), &(g_edio24cli.frame), address, count);

#define CSTR_CUR_COMMAND "ConfigMemoryW"
    } else if (0 == STRCMP_STATIC (buf, CSTR_CUR_COMMAND)) {
        address = strtol(buf + sizeof(CSTR_CUR_COMMAND), &endptr, 16);
        count = parse_hex_buf(endptr + 1, strlen(endptr + 1), buffer2, sizeof(buffer2));
        if (count < 0) {
            fprintf(stderr, "Error in parse " CSTR_CUR_COMMAND ", tcp cli ret=%" PRIiSZ ".\n", count);
        } else {
            fprintf(stderr, "dump of parameter of " CSTR_CUR_COMMAND ", size=%" PRIiSZ ":\n", count);
            hex_dump_to_fd(STDERR_FILENO, (opaque_t *)(buffer2), count);
            ret = edio24_pkt_create_cmd_confmemw (buffer1, sizeof(buffer1), &(g_edio24cli.frame), address, count, buffer2);
        }
#undef CSTR_CUR_COMMAND
#define CSTR_CUR_COMMAND "SettingsMemoryW"
    } else if (0 == STRCMP_STATIC (buf, CSTR_CUR_COMMAND)) {
        address = strtol(buf + sizeof(CSTR_CUR_COMMAND), &endptr, 16);
        count = parse_hex_buf(endptr + 1, strlen(endptr + 1), buffer2, sizeof(buffer2));
        if (count < 0) {
            fprintf(stderr, "Error in parse " CSTR_CUR_COMMAND ", tcp cli ret=%" PRIiSZ ".\n", count);
        } else {
            fprintf(stderr, "dump of parameter of " CSTR_CUR_COMMAND ", size=%" PRIiSZ ":\n", count);
            hex_dump_to_fd(STDERR_FILENO, (opaque_t *)(buffer2), count);
            ret = edio24_pkt_create_cmd_setmemw (buffer1, sizeof(buffer1), &(g_edio24cli.frame), address, count, buffer2);
        }
#undef CSTR_CUR_COMMAND
#define CSTR_CUR_COMMAND "BootloaderMemoryW"
    } else if (0 == STRCMP_STATIC (buf, CSTR_CUR_COMMAND)) {
        address = strtol(buf + sizeof(CSTR_CUR_COMMAND), &endptr, 16);
        count = parse_hex_buf(endptr + 1, strlen(endptr + 1), buffer2, sizeof(buffer2));
        if (count < 0) {
            fprintf(stderr, "Error in parse " CSTR_CUR_COMMAND ", tcp cli ret=%" PRIiSZ ".\n", count);
        } else {
            fprintf(stderr, "dump of parameter of " CSTR_CUR_COMMAND ", size=%" PRIiSZ ":\n", count);
            hex_dump_to_fd(STDERR_FILENO, (opaque_t *)(buffer2), count);
            ret = edio24_pkt_create_cmd_bootmemw (buffer1, sizeof(buffer1), &(g_edio24cli.frame), address, count, buffer2);
        }
#undef CSTR_CUR_COMMAND
#define CSTR_CUR_COMMAND "Sleep"
    } else if (0 == STRCMP_STATIC (buf, CSTR_CUR_COMMAND)) {
        count = strtol(buf + sizeof(CSTR_CUR_COMMAND), &endptr, 10);
        fprintf(stderr, "tcp cli sleep %" PRIiSZ " microseconds ...\n", count);
        usleep (count); /* Yes, I know we should use libuv's timeout callback here */
#undef CSTR_CUR_COMMAND
    }
    if (ret < 0) {
        fprintf(stderr, "tcp cli ignore line at pos(%ld): %s\n", pos, buf);
        return -1;
    }
    fprintf(stderr, "tcp cli created packet size=%" PRIiSZ ":\n", ret);
    hex_dump_to_fd(STDERR_FILENO, (opaque_t *)(buffer1), ret);
    assert (ret <= sizeof(buffer1));
    send_buffer(stream, buffer1, ret);
    g_edio24cli.num_requests ++;
    return 0;
}

void
on_tcp_cli_connect(uv_connect_t* connection, int status)
{
    uv_stream_t* stream = connection->handle;

    fprintf(stderr, "tcp cli connected.\n");

    read_file_lines (g_edio24cli.fn_conf, (void *)stream, process_command);
    uv_read_start(stream, alloc_buffer, on_tcp_cli_read);
}

/*****************************************************************************/
void
on_udp_cli_close(uv_handle_t* handle)
{
    fprintf(stderr, "udp cli closed.\n");
}

void
on_udp_cli_read(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
    if (nread < 0) {
        fprintf(stderr, "udp cli read error %s\n", uv_err_name(nread));
        uv_close(handle, on_udp_cli_close);
        free(buf->base);
        return;
    }

    if (NULL == addr) {
        fprintf (stderr, "udp cli recv from addr NULL!\n");
    } else {
        char sender[17] = { 0 };
        uv_ip4_name((const struct sockaddr_in*) addr, sender, 16);
        fprintf(stderr, "udp cli recv from %s\n", sender);

        // check the address?
        // check the response
        if ((nread == 64) && (buf->base[0] == 'D')) {
            edio24_cli_verify_udp((uint8_t *)(buf->base), nread);
            free(buf->base);
            return;

        } else if ((nread == 2) && (buf->base[0] == 'C')) {
            if (buf->base[1] == 0) {
                uv_ip4_name((const struct sockaddr_in*) &(g_edio24cli.addr_tcp), sender, 16);
                fprintf(stderr, "tcp cli connect to %s:%d\n", sender, ntohs(g_edio24cli.addr_tcp.sin_port));
                // start TCP connection
                uv_tcp_connect(&(g_edio24cli.connect), &(g_edio24cli.uvtcp), (const struct sockaddr*)&(g_edio24cli.addr_tcp), on_tcp_cli_connect);
            } else {
                fprintf(stderr, "udp cli return failed: 0x%02X 0x%02X(%s)\n", buf->base[0], buf->base[1], edio24_val2cstr_status(buf->base[1]));
            }
        } else {
            fflush(stderr);
            fsync(STDERR_FILENO);
            fprintf(stderr, "udp cli recv size(%" PRIiSZ ") != 2\n", nread);
            hex_dump_to_fd(STDERR_FILENO, (opaque_t *)(buf->base), nread);
        }
    }

    free(buf->base);
    uv_udp_recv_stop(handle);
}

void
on_udp_cli_send(uv_udp_send_t *req, int status)
{
    if (status) {
        fprintf(stderr, "Send error %s\n", uv_strerror(status));
        return;
    }
    uv_udp_recv_start(req->handle, alloc_buffer, on_udp_cli_read);
}

static char flg_has_error = 0;

static void
idle_cb (uv_idle_t *handle)
{
    time_t curtime;
    time(&curtime);
    if (g_edio24cli.starttime + g_edio24cli.timeout <= curtime) {
        flg_has_error = 1;
        uv_idle_stop(handle);
        uv_stop(uv_default_loop());
        fprintf(stderr, "timeout: %d\n", (int)g_edio24cli.timeout);
    }
}

static void
on_uv_close(uv_handle_t* handle)
{
    if (handle != NULL) {
        //delete handle;
    }
}

static void
on_uv_walk(uv_handle_t* handle, void* arg)
{
    uv_close(handle, on_uv_close);
}

static void
on_sigint_received(uv_signal_t *handle, int signum)
{
    int result = uv_loop_close(handle->loop);
    if (result == UV_EBUSY) {
        uv_walk(handle->loop, on_uv_walk, NULL);
    }
}

int
main_cli(const char * host, int port_udp, int port_tcp, time_t timeout, char flg_discovery, const char * fn_conf)
{
    int ret = 0;
    struct sockaddr_in broadcast_addr;
    struct sockaddr_in addr_udp;
    uv_udp_t uvudp;
    uint32_t connect_code = 0;
    uv_idle_t idler;
    uv_signal_t sigint;

    // setup service related info
    memset (&g_edio24cli, 0, sizeof (g_edio24cli));
    g_edio24cli.frame = 0;
    g_edio24cli.sz_data = 0;
    g_edio24cli.num_requests = 0;
    g_edio24cli.num_responds = 0;
    g_edio24cli.fn_conf = fn_conf;
    uv_ip4_addr(host, port_tcp, &(g_edio24cli.addr_tcp));

    loop = uv_default_loop();
    assert (NULL != loop);

    uv_signal_init(loop, &sigint);
    uv_signal_start(&sigint, on_sigint_received, SIGINT);
    if (timeout > 0) {
        uv_idle_init(loop, &idler);
        uv_idle_start(&idler, idle_cb);
    }
    time (&(g_edio24cli.starttime));
    g_edio24cli.timeout = timeout;

    uv_tcp_init(loop, &(g_edio24cli.uvtcp));
    uv_tcp_keepalive(&(g_edio24cli.uvtcp), 1, 60);

    // setup the UDP client
    uv_ip4_addr(host, port_udp, &(addr_udp));
    uv_udp_init(loop, &uvudp);

    // support broadcast addresses
    uv_ip4_addr("0.0.0.0", 0, &broadcast_addr);
    uv_udp_bind(&uvudp, (const struct sockaddr *)&broadcast_addr, 0);
    uv_udp_set_broadcast(&uvudp, 1);

    uv_udp_send_t send_req;
    uv_buf_t msg;
    alloc_buffer((uv_handle_t*)&send_req, 5, &msg);
    if (flg_discovery) {
        msg.len = edio24_pkt_create_discoverydev((uint8_t *)(msg.base), msg.len);
    } else {
        edio24_pkt_create_opendev((uint8_t *)(msg.base), msg.len, connect_code);
    }
    uv_udp_send(&send_req, &uvudp, &msg, 1, (const struct sockaddr *)&addr_udp, on_udp_cli_send);

    ret = uv_run(loop, UV_RUN_DEFAULT);
    // uv_signal_stop(&sigint);
    if (ret != 0) {
        return ret;
    }
    if (flg_has_error) {
        return 1;
    }
    return 0;
}

/*****************************************************************************/
static void
version (void)
{
    printf ("E-DIO24 client v%d.%d\n", EDIO24CLI_MAIN, EDIO24CLI_MINOR);
}

static void
help(const char * progname)
{
    printf ("Usage: \n"
            "\t%s [-h] [-v] [-d] [-t <TCP port>] [-u <UDP port>] [-a '<bind addr>']\n"
            , basename(progname));
    printf ("\nOptions:\n");
    printf ("\t-r <addr>\tE-DIO24 device address\n");
    printf ("\t-t <port>\tE-DIO24 command (TCP) listen port\n");
    printf ("\t-u <port>\tE-DIO24 discover (UDP) listen port\n");
    printf ("\t-e <cmd file>\tExecute the command lines in the file\n");
    printf ("\t-m <time>\tthe seconds of timeout\n");
    printf ("\t-d\tDiscovery devices\n");
    printf ("\t-h\tPrint this message.\n");
    printf ("\t-v\tVerbose information.\n");
}

static void
usage (char *progname)
{
    version ();
    help (progname);
}

int
main(int argc, char * argv[])
{
    char flg_verbose = 0;
    char flg_discovery = 0;
    const char * host = "127.0.0.1";
    int port_udp = EDIO24_PORT_DISCOVER;
    int port_tcp = EDIO24_PORT_COMMAND;
    const char * fn_conf = NULL;
    time_t timeout = 0;

    int c;
    struct option longopts[]  = {
        { "address",      1, 0, 'r' },
        { "portudp",      1, 0, 'u' },
        { "porttcp",      1, 0, 't' },
        { "execute",      1, 0, 'e' },
        { "discovery",    0, 0, 'd' },
        { "timeout",      1, 0, 'm' },

        { "help",         0, 0, 'h' },
        { "verbose",      0, 0, 'v' },
        { 0,              0, 0,  0  },
    };

    while ((c = getopt_long( argc, argv, "r:u:t:e:m:dhv", longopts, NULL )) != EOF) {
        switch (c) {
            case 'm':
                if (strlen (optarg) > 0) {
                    timeout = atoi(optarg);
                }
                break;
            case 'r':
                if (strlen (optarg) > 0) {
                    host = optarg;
                }
                break;
            case 't':
                if (strlen (optarg) > 0) {
                    port_tcp = atoi(optarg);
                }
                break;
            case 'u':
                if (strlen (optarg) > 0) {
                    port_udp = atoi(optarg);
                }
                break;
            case 'e':
                if (strlen (optarg) > 0) {
                    fn_conf = optarg;
                }
                break;
            case 'd':
                flg_discovery = 1;
                break;

            case 'h':
                usage (argv[0]);
                exit (0);
                break;
            case 'v':
                flg_verbose = 1;
                break;
            default:
                fprintf (stderr, "Unknown parameter: '%c'.\n", c);
                fprintf (stderr, "Use '%s -h' for more information.\n", basename(argv[0]));
                exit (-1);
                break;
        }
    }
    (void)flg_verbose;

    return main_cli(host, port_udp, port_tcp, timeout, flg_discovery, fn_conf);
}
