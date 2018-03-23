/**
 * \file    edio24sim.c
 * \brief   a E-DIO24 device simulator
 * \author  Yunhui Fu <yhfudev@gmail.com>
 * \version 1.0
 */

#define EDIO24SIM_MAIN  1
#define EDIO24SIM_MINOR 0

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // STDERR_FILENO
#include <libgen.h> // basename()
#include <string.h> // memmove
#include <getopt.h>
#include <assert.h>
#include <uv.h>

#include "libedio24.h"

#if DEBUG
#include "hexdump.h"
#else
#define hex_dump_to_fd(fd, fragment, size)
#endif

#define DEFAULT_BACKLOG 128

uv_loop_t * loop = NULL; /**< this have to be global variable, since it needs to access in on_xxxx() when service new connections */

typedef struct _edio24svr_t {
    char flg_used; /**< 1 -- if the service in busy, only one connect were allowed */
    char flg_randfail; /**< 1 -- if the server send out fail message on requests */

    size_t sz_data; /**< the lenght of data in the buffer */
    uint8_t buffer[(EDIO24_PKT_LENGTH_MIN + 6) * 5]; /**< the buffer to cache the received packets */
} edio24svr_t;

edio24svr_t g_edio24svr;

void
alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

void
realloc_buffer(size_t suggested_size, uv_buf_t *buf)
{
    if (buf->base && (buf->len >= suggested_size)) {
        return;
    }
    buf->base = realloc(buf->base, suggested_size);
    buf->len = suggested_size;
}

/*****************************************************************************/

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

/*****************************************************************************/
void
on_udp_svr_close(uv_handle_t* handle)
{
    fprintf(stderr, "udp svr closed.\n");
}

void
on_udp_svr_write(uv_udp_send_t *req, int status)
{
    if (status) {
        fprintf(stderr, "udp svr write error %s\n", uv_strerror(status));
    } else {
        printf("wrote.\n");
    }
    udp_send_buf_free ((udp_send_buf_t *)req);
}

void
on_udp_srv_read(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags)
{
    char flg_randfail = 0;
    if (g_edio24svr.flg_randfail) {
        if (rand() % 100 < 50) {
            flg_randfail = 1;
        }
    }

    fprintf(stderr, "udp svr read %" PRIiSZ "\n", nread);
    if (nread < 0) {
        fprintf(stderr, "udp svr read error %s\n", uv_err_name(nread));
        uv_close(handle, on_udp_svr_close);
        free(buf->base);
        return;
    }

    if (NULL == addr) {
        fprintf (stderr, "udp svr recv from addr NULL!\n");
    } else {
        char sender[17] = { 0 };
        struct sockaddr_in *paddr = (struct sockaddr_in *)addr;
        uv_ip4_name((const struct sockaddr_in*) addr, sender, sizeof(sender) - 1);
        fprintf(stderr, "udp svr recv from addr %s, port %d\n", sender, ntohs(paddr->sin_port));
    }

    if ((NULL != buf) && (NULL != buf->base)) {
        if ((nread == 1) && ('D' == buf->base[0])) {
            /// discovery message
            int ret;
            size_t sz_out;
            size_t sz_needed_out;
            udp_send_buf_t *req = (udp_send_buf_t*) malloc(sizeof(udp_send_buf_t));
            req->buf = uv_buf_init(buf->base, 64);
            sz_out = 64;
            sz_needed_out = 0;

            ret = edio24_svr_process_udp(flg_randfail, (uint8_t *)(buf->base), nread, (uint8_t *)(buf->base), &sz_out, &sz_needed_out);
            fprintf(stderr, "udp svr discovery process ret=%d, needout=%" PRIuSZ ", flg_randfail=%s; g_flg_randfail=%s\n", ret, sz_needed_out, (flg_randfail?"use fail":"use normal"), (g_edio24svr.flg_randfail?"use fail":"use normal"));
            if ((sz_out > 0) && (NULL != addr)) {
                fprintf(stderr, "udp svr send back sz=%" PRIuSZ "\n", sz_out);
                int r = uv_udp_send(req, handle, &req->buf, 1, (const struct sockaddr *)addr, on_udp_svr_write);
                if (r) {
                    /* error */
                    fprintf(stderr, "udp svr error in write() %s\n", uv_strerror(r));
                    free(buf->base);
                }
            }

        } else if ((nread == 5) && ('C' == buf->base[0])) {
            /// start of new command session
            udp_send_buf_t *req = (udp_send_buf_t*) malloc(sizeof(udp_send_buf_t));
            req->buf = uv_buf_init(buf->base, 2);
            req->buf.base[0] = 'C'; //0x43;
            if (g_edio24svr.flg_used) {
                req->buf.base[1] = 1;
            } else {
                req->buf.base[1] = 0;
                if (flg_randfail) {
                    req->buf.base[1] = 1;
                }
            }
            //req->buf.len = 2;
            if (NULL != addr) {
                int r = uv_udp_send(req, handle, &req->buf, 1, (const struct sockaddr *)addr, on_udp_svr_write);
                if (r) {
                    /* error */
                    fprintf(stderr, "udp svr error in write() %s\n", uv_strerror(r));
                    free(buf->base);
                }
            }
        } else {
            // ignore
            fflush(stderr);
            fsync(STDERR_FILENO);
            fprintf(stderr, "udp svr recv size(%" PRIuSZ ") != 5\n", nread);
            hex_dump_to_fd(STDERR_FILENO, (opaque_t *)(buf->base), nread);
            free(buf->base);
            //buf->base = NULL; buf->len = 0;
        }
    }

    //uv_udp_recv_stop(handle);
}

/*****************************************************************************/

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

/*****************************************************************************/
void
on_tcp_svr_close(uv_handle_t* handle)
{
    fprintf(stderr, "tcp svr closed.\n");
    g_edio24svr.flg_used = 0;
}

void
on_tcp_svr_write(uv_write_t* req, int status)
{
    if (status) {
        fprintf(stderr, "tcp svr write error %s\n", uv_strerror(status));
    }
    write_buf_free((write_buf_t *)req);
}

/**
 * \brief process client packet in the buffer of edio24svr_t
 * \param ped: the edio24svr with buffer
 * \param stream: the libuv socket
 *
 * \return 0 on successs
 *
 * process the data in the buffer, and send response when possible,
 * until there's no more data can be processed in one round
 */
ssize_t
edio24svr_process_data (edio24svr_t * ped, uv_stream_t *stream)
{
    uint8_t * buffer_in = NULL;
    size_t sz_in;
    uint8_t * buffer_out = NULL;
    size_t sz_out;
    size_t sz_processed;
    size_t sz_needed_in;
    size_t sz_needed_out;

    //int flg_again = 0;
    int ret;
    uv_buf_t buf;
    char flg_randfail = 0;

    assert (NULL != ped);
    assert (NULL != stream);

    alloc_buffer (NULL, 7+30, &buf);

    while(1) { // while (ped->sz_data > 0) {
        buffer_in = ped->buffer;
        sz_in = ped->sz_data;
        buffer_out = (uint8_t *)(buf.base);
        sz_out = buf.len;
        //flg_again = 0;
        sz_processed = 0;
        sz_needed_in = 0;
        sz_needed_out = 0;

        assert (NULL != buffer_in);
        assert (sz_in >= 0);
        assert (NULL != buffer_out);
        assert (sz_out > 0);
        flg_randfail = 0;
        if (ped->flg_randfail) {
            if (rand() % 100 < 50) {
                flg_randfail = 1;
            }
        }
        ret = edio24_svr_process_tcp(flg_randfail, buffer_in, sz_in, buffer_out, &sz_out,
                                 &sz_processed, &sz_needed_in, &sz_needed_out);
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
        if (sz_needed_out > 0) {
            // extend the buffer_out
            fprintf(stderr, "need more out buffer: %" PRIuSZ "\n", sz_needed_out);
            realloc_buffer(buf.len + sz_needed_out, &buf);
            //flg_again = 1;
        }
        if (sz_out > 0) {
            fprintf(stderr, "send out packet size=%" PRIuSZ "\n", sz_out);
            write_buf_t *req = (write_buf_t*) malloc(sizeof(write_buf_t));
            alloc_buffer(NULL, sz_out, &(req->buf));
            memmove (req->buf.base, buffer_out, sz_out);
            assert ((uint8_t *)(buf.base) == buffer_out);
            int r = uv_write((uv_write_t*) req, stream, &req->buf, 1, on_tcp_svr_write);
            if (r) {
                /* error */
                fprintf(stderr, "tcp svr error in write() %s\n", uv_strerror(r));
            }
        }
        if (ret < 0) {
            break;
        } else if (ret == 0) {
            //flg_again = 1;
        } else if (ret == 1) {
            //flg_again = 1;
        } else if (ret == 2) {
            break;
        }
    }
    if (buf.base) {
        free (buf.base);
    }
    return 0;
}

void
on_tcp_svr_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    if (nread > 0) {
        // push the content to a center buffer for this TCP connection
        // and fetch the cmd to fetch appriate length of data as a packet
        // to process
        size_t sz_copy;
        size_t sz_processed;

        fprintf(stderr,"tcp svr read block:\n");
        hex_dump_to_fd(STDERR_FILENO, (opaque_t *)(buf->base), nread);

        fprintf(stderr, "tcp svr process data 1\n");
        edio24svr_process_data (&g_edio24svr, stream);
        sz_processed = 0;
        while (sz_processed < nread) {
            sz_copy = nread - sz_processed;
            assert (sizeof(g_edio24svr.buffer) >= g_edio24svr.sz_data);
            if (sz_copy + g_edio24svr.sz_data > sizeof(g_edio24svr.buffer)) {
                sz_copy = sizeof(g_edio24svr.buffer) - g_edio24svr.sz_data;
            }
            fprintf(stderr, "tcp svr push received data size=%" PRIuSZ ", processed=%" PRIuSZ "\n", sz_copy, sz_processed);
            if (sz_copy > 0) {
                memmove (g_edio24svr.buffer + g_edio24svr.sz_data, buf->base + sz_processed, sz_copy);
                sz_processed += sz_copy;
                g_edio24svr.sz_data += sz_copy;
            } else {
                // error? full?
                fprintf(stderr, "tcp svr no more received data to be push\n");
                break;
            }
            fprintf(stderr, "tcp svr process data 2\n");
            edio24svr_process_data (&g_edio24svr, stream);
        }
        if (sz_processed < nread) {
            // we're stalled here, because the content can't be processed by the function edio24svr_process_data()
            // error
            fprintf(stderr, "tcp svr data stalled\n");
            uv_close((uv_handle_t *)stream, on_tcp_svr_close);
        }
    }
    if (nread == 0) {
        fprintf(stderr,"tcp svr read zero!\n");
    }
    if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "tcp svr read error %s\n", uv_err_name(nread));
        }

        fprintf(stderr,"tcp svr close remote!\n");
        uv_close((uv_handle_t *)stream, on_tcp_svr_close);
    }

    fprintf(stderr, "tcp svr read end\n");
    free(buf->base);
}

void
on_tcp_svr_accept (uv_stream_t *server, int status)
{
    uv_tcp_t *client;

    if (status < 0) {
        fprintf(stderr, "tcp svr new connection error %s\n", uv_strerror(status));
        // error!
        return;
    }

    fprintf(stderr, "tcp svr accept()\n");
    if (g_edio24svr.flg_used) {
        fprintf(stderr, "device busy\n");
        return;
    }
    g_edio24svr.flg_used = 1;

    client = malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    if (uv_accept(server, (uv_stream_t *)client) == 0) {

        int r = uv_read_start((uv_stream_t *)client, alloc_buffer, on_tcp_svr_read);
        if (r) {
            /* error */
            fprintf(stderr, "tcp svr accept client error %s\n", uv_strerror(status));
        }
    } else {
        g_edio24svr.flg_used = 0;
        fprintf(stderr, "tcp svr failed at accept, close client socket\n");
        uv_close((uv_handle_t *)client, on_tcp_svr_close);
    }
}

/*****************************************************************************/
int
main_svr(const char * host, int port_udp, int port_tcp, char flg_randfail)
{
    struct sockaddr_in addr_udp;
    struct sockaddr_in addr_tcp;
    uv_udp_t uvudp;
    uv_tcp_t uvtcp;

    // setup service related info
    memset (&g_edio24svr, 0, sizeof (g_edio24svr));
    g_edio24svr.flg_used = 0;
    g_edio24svr.sz_data = 0;
    g_edio24svr.flg_randfail = flg_randfail;

    loop = uv_default_loop();

    // setup the UDP listen port
    uv_ip4_addr(host, port_udp, &addr_udp);
    uv_udp_init(loop, &uvudp);
    uv_udp_bind(&uvudp, (const struct sockaddr *)&addr_udp, UV_UDP_REUSEADDR);
    uv_udp_recv_start(&uvudp, alloc_buffer, on_udp_srv_read);

    // setup the TCP listen port
    uv_ip4_addr(host, port_tcp, &addr_tcp);
    uv_tcp_init(loop, &uvtcp);
    uv_tcp_bind(&uvtcp, (const struct sockaddr*)&addr_tcp, 0);

    int r = uv_listen((uv_stream_t *)&uvtcp, DEFAULT_BACKLOG, on_tcp_svr_accept);
    if (r) {
        fprintf(stderr, "tcp svr listen error %s\n", uv_strerror(r));
        return 1;
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}

/*****************************************************************************/
static void
version (void)
{
    printf ("E-DIO24 simulator (server) v%d.%d\n", EDIO24SIM_MAIN, EDIO24SIM_MINOR);
}

static void
help(const char * progname)
{
    printf ("Usage: \n"
            "\t%s [-h] [-v] [-t <TCP port>] [-u <UDP port>] [-a '<bind addr>']\n"
            , basename(progname));
    printf ("\nOptions:\n");
    printf ("\t-a <addr>\tE-DIO24 device address\n");
    printf ("\t-t <port>\tE-DIO24 command (TCP) listen port\n");
    printf ("\t-u <port>\tE-DIO24 discover (UDP) listen port\n");
    printf ("\t-l\tSend out fail message randomly on requests.\n");
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
    char flg_randfail = 0;
    const char * host = "0.0.0.0";
    int port_udp = EDIO24_PORT_DISCOVER;
    int port_tcp = EDIO24_PORT_COMMAND;

    int c;
    struct option longopts[]  = {
        { "address",      1, 0, 'a' },
        { "portudp",      1, 0, 'u' },
        { "porttcp",      1, 0, 't' },

        { "randomfail",   0, 0, 'l' },

        { "help",         0, 0, 'h' },
        { "verbose",      0, 0, 'v' },
        { 0,              0, 0,  0  },
    };

    while ((c = getopt_long( argc, argv, "a:u:t:lhv", longopts, NULL )) != EOF) {
        switch (c) {
            case 'a':
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
            case 'l':
                flg_randfail = 1;
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

    return main_svr(host, port_udp, port_tcp, flg_randfail);
}
