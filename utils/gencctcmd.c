/**
 * \file    gencctcmd.c
 * \brief   generate E-DIO24 command sequence
 * \author  Yunhui Fu <yhfudev@gmail.com>
 * \version 1.0
 */

#include <stdio.h>
#include <stdlib.h> // atoi
#include <stdint.h> // uint8_t
#include <ctype.h> // isdigit()
#include <string.h>
#include <getopt.h>
#include <assert.h>

#include "libedio24.h" // PRIuSZ
#include "utils.h"

#define EDIO24_INVALID_INT (-1)

/**
 * \brief parse the atten arg
 * \param arg: the input string
 * \param mask_pin: the return mask for 'p', will store 32 positions 0-31.
 * \param val: the return value for 'v'
 * for example:
 *    "p=0,1,2;v=31" -- pin 0, 1, and 2; value 31
 */
int
parse_values_atten (const char * arg, unsigned int * mask_pin, unsigned int * val)
{
    char * p = NULL;
    unsigned int hval=0;
    int type = 0;
    int got = 0;

    assert (NULL != arg);
    assert (NULL != mask_pin);
    assert (NULL != val);

    *mask_pin = 0;
    //*val = 0;

    int len = strlen(arg);
    if (len < 1) {
        return -1;
    }

    type = 0;
    for (p = (char *)arg; p < arg + len; p ++) {

        if (0 == strncmp(p, "p=", 2)) {
            type = 1; // mask_pin
            got = 0;
            p += 1;
            continue;

        } else if (0 == strncmp(p, "v=", 2)) {
            type = 2; // val
            got = 0;
            p += 1;
            continue;

        } else if (0 == strncmp(p, "all", 3)) {
            if (type != 1) {
                fprintf(stderr, "Error atten arg, invalid values only for 'p'");
                return -1;
            }
            *mask_pin = 0xFFFFFFFF;
            p += 3;
            type = 0;
            got = 1;
            continue;

        } else if (*p == ',') {
            continue;

        } else if (*p == ';') {
            type = 0;
            continue;

        } else if (isdigit(*p)) {
            hval = atoi(p);
            for (; isdigit(*p); p++) {
            }
        } else {
            fprintf(stderr, "parse error for char '%c'\n", *p);
            return -1;
        }
        if (type == 1) {
            *mask_pin |= (0x01 << hval);
        } else if (type == 2) {
            *val = hval;
        }
        got = 1;
    }
    if (type == 2 && ! got) {
        return -1;
    }

    return 0;
}

/**
 * \brief parse the power arg
 * \param arg: the input string
 * \param mask_pin: the return mask for 'p'
 * \param val: the return value for 'v'
 *
 * examples
 *  -# "p=0,1,2" -- pin 0, 1, and 2 will be ON.
 *  -# "p=all" -- all ON
 *  -# "p=" -- all off
 */
int
parse_values_power (const char * arg, unsigned int * mask_pin)
{
    int ret;
    unsigned int val = EDIO24_INVALID_INT;
    assert (NULL != arg);
    assert (NULL != mask_pin);

    ret = parse_values_atten(arg, mask_pin, &val);
    if (ret < 0) {
        fprintf(stderr, "Error in parse_values_atten(arg='%s') return %d\n", arg, ret);
        return ret;
    }
    if (val != EDIO24_INVALID_INT) {
        fprintf(stderr, "Error in parse_values_atten(arg='%s') return val=%d !=0\n", arg, val);
        return -1;
    }

    return 0;
}

int
output_power(FILE * outf, void * user_arg)
{
    char * arg = (char *)user_arg;
    unsigned int mask_pin = 1;

    assert (NULL != outf);
    //fprintf (stderr, "output_power: arg='%s'\n", user_arg);

    if (0 != parse_values_power (arg, &mask_pin)) {
        fprintf (stderr, "Error: parse the power values arg='%s'\n", arg);
        return 1;
    }
    mask_pin &= 0xFF;
    fprintf (stderr, "output_power: arg='%s', pin=0x%02X\n", arg, mask_pin);

    // set power at port 0:
    // all pin output
    fprintf (outf, "DConfigW 0xFFFFFF 0x000000\n");

    // port 0, if 1 -- power on, 0 -- power off
    // pin 3 power on
    fprintf (outf, "DOutW 0x0000FF 0x%06X\n", (mask_pin & 0xFF));

    fprintf (outf, "\n");
    return 0;
}

int
output_power_read(FILE * outf, void * user_arg)
{
    char * arg = (char *)user_arg;
    assert (NULL != outf);
    fprintf (stderr, "output_power_read: arg='%s'\n", arg);

    fprintf (outf, "DOutR\n"); // & 0xFF
    fprintf (outf, "\n");
    return 0;
}

int
output_atten(FILE * outf, void * user_arg)
{
    char * arg = (char *)user_arg;
    unsigned int mask_pin = 1;
    unsigned int val = 31;
    uint32_t hval = 0x01;

    assert (NULL != outf);
    //fprintf (stderr, "output_atten: arg='%s'\n", user_arg);

    if (0 != parse_values_atten (arg, &mask_pin, &val)) {
        fprintf (stderr, "Error: parse the atten values: '%s'\n", arg);
        return 1;
    }
    mask_pin &= 0x00FF;
    fprintf (stderr, "output_atten: arg='%s', pin=0x%02X, val=0x%04X\n", arg, mask_pin, val);

    // all pin output
    fprintf (outf, "DConfigW 0xFFFFFF 0x000000\n");

    // tha latch for atten are at port 1
    // the data port of atten are at port 2
    // pin low, for example, pin 3
    hval = (mask_pin & 0xFF) << 8;
    fprintf (outf, "DOutW 0x%06X 0x000000\n", hval);

    // set the value 31 at port 2
    hval = (val & 0xFF) << 16;
    fprintf (outf, "DOutW 0xFF0000 0x%06X\n", hval);

    // usleep 1
    fprintf (outf, "Sleep 1\n");

    // pin high, for pin 3
    hval = (mask_pin & 0xFF) << 8;
    fprintf (outf, "DOutW 0x%06X 0x%06X\n", hval, hval);

    // usleep 1
    fprintf (outf, "Sleep 1\n");

    // pin low again
    hval = (mask_pin & 0xFF) << 8;
    fprintf (outf, "DOutW 0x%06X 0x000000\n", hval);

    // set the data bus(port 2) to low
    fprintf (outf, "DOutW 0xFF0000 0x000000\n");

    // usleep 1
    //fprintf (outf, "Sleep 1\n");

    fprintf (outf, "\n");
    return 0;
}

#if defined(CIUT_ENABLED) && (CIUT_ENABLED == 1)
#include <ciut.h>
#include <ciut-sio.h>

TEST_CASE( .description="test gencctcmd parse_xxx.", .skip=0 ) {

    char * arg = NULL;
    unsigned int pin;
    unsigned int val;

#if 0
    SECTION("test parser atten's invalid arg") {
        //CIUT_LOG ("atten invalid arg");

        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 > parse_values_atten(NULL, NULL, NULL));
        REQUIRE(0 > parse_values_atten(arg, &pin, &val));
        REQUIRE(1 == pin);
        REQUIRE(EDIO24_INVALID_INT == val);
    }
#endif

    SECTION("test parser atten's valid p arg null") {
        arg = "p=";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0 == pin);
        REQUIRE(EDIO24_INVALID_INT == val);
    }

    SECTION("test parser atten's valid p arg") {
        arg = "p=0";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0x01 == pin);
        REQUIRE(EDIO24_INVALID_INT == val);

        arg = "p=0,1,2";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0x07 == pin);
        REQUIRE(EDIO24_INVALID_INT == val);

        arg = "p=0,1,2;";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0x07 == pin);
        REQUIRE(EDIO24_INVALID_INT == val);

        arg = "p=all";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0xFFFFFFFF == pin);
        REQUIRE(EDIO24_INVALID_INT == val);
    }

    SECTION("test parser atten's valid v arg") {
        arg = "v";
        pin = 1; val = 1;
        REQUIRE(0 > parse_values_atten(arg, &pin, &val));

        arg = "v=";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 > parse_values_atten(arg, &pin, &val));
        REQUIRE(0 == pin);
        REQUIRE(EDIO24_INVALID_INT == val);

        arg = "v=;";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0  == pin);
        REQUIRE(EDIO24_INVALID_INT == val);

        arg = "v=21";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0  == pin);
        REQUIRE(21 == val);

        arg = "v=21;";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0  == pin);
        REQUIRE(21 == val);

        arg = "v=21,9";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0  == pin);
        REQUIRE(9 == val);
    }

    SECTION("test parser atten's mix arg") {
        //CIUT_LOG ("atten invalid arg");

        arg = "p=0,1,2;v=11";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0x07 == pin);
        REQUIRE(11 == val);

        arg = "v=21;p=1,3,all";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_atten(arg, &pin, &val));
        REQUIRE(0xFFFFFFFF == pin);
        REQUIRE(21 == val);
    }

    SECTION("test parser power's valid arg") {
        arg = "p=;";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_power(arg, &pin));
        REQUIRE(0 == pin);

        arg = "p=;v";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 > parse_values_power(arg, &pin));

        arg = "p=;v=";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 > parse_values_power(arg, &pin));

        arg = "p=;";
        pin = 1; val = EDIO24_INVALID_INT;
        REQUIRE(0 == parse_values_power(arg, &pin));
        REQUIRE(0 == pin);
    }
    //CIUT_DBL_EQUAL(0.1 + 0.2, 0.3);
}

#ifndef ARRAY_NUM
#define ARRAY_NUM(a) (sizeof(a)/sizeof(a[0]))
#endif

TEST_CASE( .description="test gencctcmd output_xxx.", .skip=0 ) {

    int i;

    static const char * iodata_power[] = {
        "p=",       "DConfigW 0xFFFFFF 0x000000\nDOutW 0x0000FF 0x000000\n\n",
        "p=0,1,2",  "DConfigW 0xFFFFFF 0x000000\nDOutW 0x0000FF 0x000007\n\n",
        "p=all",    "DConfigW 0xFFFFFF 0x000000\nDOutW 0x0000FF 0x0000FF\n\n",
    };
    static const char * iodata_atten[] = {
        "p=all;v=31",   "DConfigW 0xFFFFFF 0x000000\nDOutW 0x00FF00 0x000000\nDOutW 0xFF0000 0x1F0000\nSleep 1\nDOutW 0x00FF00 0x00FF00\nSleep 1\nDOutW 0x00FF00 0x000000\nDOutW 0xFF0000 0x000000\n\n",
        "p=0,1,2;v=31", "DConfigW 0xFFFFFF 0x000000\nDOutW 0x000700 0x000000\nDOutW 0xFF0000 0x1F0000\nSleep 1\nDOutW 0x000700 0x000700\nSleep 1\nDOutW 0x000700 0x000000\nDOutW 0xFF0000 0x000000\n\n",
    };
    static const char * iodata_power_read[] = {
        "",   "DOutR\n\n",
    };

    SECTION("test output atten's valid p arg") {
        for (i = 0; i < ARRAY_NUM(iodata_atten); i += 2) {
            CIUT_LOG ("output atten idx=%d", i/2);
            REQUIRE(0 == cuit_check_output(output_atten, iodata_atten[i + 0], iodata_atten[i + 1]));
        }
    }
    SECTION("test output power's valid p arg") {
        for (i = 0; i < ARRAY_NUM(iodata_power); i += 2) {
            CIUT_LOG ("output power idx=%d", i/2);
            REQUIRE(0 == cuit_check_output(output_power, iodata_power[i + 0], iodata_power[i + 1]));
        }
    }
    SECTION("test output power read") {
        for (i = 0; i < ARRAY_NUM(iodata_power_read); i += 2) {
            CIUT_LOG ("output power idx=%d", i/2);
            REQUIRE(0 == cuit_check_output(output_power_read, iodata_power_read[i + 0], iodata_power_read[i + 1]));
        }
    }
}

#endif /* CIUT_ENABLED */


/*****************************************************************************/
// read the config files

typedef struct _board_list_t {
    size_t sz_max;
    size_t sz_cur;
    char ** list;
} board_list_t;

#define BRDLST_DECLARE_INIT(a) board_list_t a = {0, 0, NULL}

/**
 * \brief init the structure
 * \param plst: the pointer to a structure
 */
void
brdlst_init(board_list_t * plst)
{
    assert (NULL != plst);
    memset(plst, 0, sizeof(*plst));
}

/**
 * \brief clean the structure
 * \param plst: the pointer to a structure
 */
void
brdlst_clean(board_list_t * plst)
{
    assert (NULL != plst);
    if (plst->list) {
        int i;
        assert (plst->sz_cur > 0);
        assert (plst->sz_max >= plst->sz_cur);
        fprintf(stderr, "clean the brdlst list: sz=%" PRIuSZ ", max=%" PRIuSZ "\n", plst->sz_cur, plst->sz_max);
        for (i = 0; i < plst->sz_cur; i ++) {
            assert (NULL != plst->list[i]);
            free (plst->list[i]);
        }
        free (plst->list);
    }
    memset(plst, 0, sizeof(*plst));
}

/**
 * \brief get number of record rows in the structure
 * \param plst: the pointer to a structure
 * \return the number of records
 */
ssize_t
brdlst_length(board_list_t * plst)
{
    assert (NULL != plst);
    return plst->sz_cur;
}

/**
 * \brief get current maximum size of the buffer in the structure
 * \param plst: the pointer to a structure
 * \return the size of buffer
 */
ssize_t
brdlst_maxsize(board_list_t * plst)
{
    assert (NULL != plst);
    return plst->sz_max;
}

/**
 * \brief get row by id from the structure
 * \param plst: the pointer to a structure
 * \param id: the index of the record
 * \return the string in the structure, NULL on failed
 */
char *
brdlst_get(board_list_t * plst, int id)
{
    assert (NULL != plst);
    if (id >= plst->sz_cur) {
        return NULL;
    }
    return plst->list[id];
}

/**
 * \brief append a row to the structure
 * \param plst: the pointer to a structure
 * \param cstr: the string row to be appended
 * \return 0 on success, <0 on failed
 */
int
brdlst_append(board_list_t * plst, const char * cstr)
{
    char * cstrdup = NULL;
    assert (NULL != plst);
    if (NULL == cstr) {
        return -1;
    }
    if (plst->sz_cur >= plst->sz_max) {
        assert (plst->sz_cur == plst->sz_max);
        // double the size
        void * list_new;
        size_t sz_buf = 20;
        if (sz_buf <= plst->sz_max) {
            sz_buf = plst->sz_max * 2;
        }
        assert (sz_buf > 0);
        assert (sz_buf > plst->sz_max);
        list_new = realloc(plst->list, sizeof(char *) * sz_buf);
        if (! list_new) {
            fprintf(stderr, "error in double memory\n");
            return -1;
        }
        plst->list = (char **)list_new;
        plst->sz_max = sz_buf;
    }
    assert (plst->sz_max > plst->sz_cur);
    // dup the string
    assert (NULL != cstr);
    cstrdup = strdup(cstr);
    if (NULL == cstrdup) {
        fprintf(stderr, "error in dup c string: '%s'\n", cstr);
        return -1;
    }
    plst->list[plst->sz_cur] = cstrdup;
    plst->sz_cur ++;
    return 0;
}

#if defined(CIUT_ENABLED) && (CIUT_ENABLED == 1)

#include <ciut.h>

TEST_CASE( .description="test board list.", .skip=0 ) {

    SECTION("test paprameters") {
        //CIUT_LOG ("test static declaration");
        BRDLST_DECLARE_INIT(lst_test);
        REQUIRE(0 == brdlst_maxsize(&lst_test));
        REQUIRE(0 == brdlst_length(&lst_test));

        REQUIRE(-1 == brdlst_append(&lst_test, NULL));
        REQUIRE(0 == brdlst_length(&lst_test));

#define CSTR_TEST ""
        REQUIRE(0 == brdlst_append(&lst_test, CSTR_TEST));
        REQUIRE(0 < brdlst_maxsize(&lst_test));
        REQUIRE(1 == brdlst_length(&lst_test));
        REQUIRE(0 == strcmp(CSTR_TEST, brdlst_get(&lst_test, 0)));
#undef CSTR_TEST

#define CSTR_TEST "abc"
        REQUIRE(0 == brdlst_append(&lst_test, CSTR_TEST));
        REQUIRE(0 < brdlst_maxsize(&lst_test));
        REQUIRE(2 == brdlst_length(&lst_test));
        REQUIRE(0 == strcmp(CSTR_TEST, brdlst_get(&lst_test, 1)));
#undef CSTR_TEST

        brdlst_clean(&lst_test);
        REQUIRE(0 == lst_test.sz_max);
        REQUIRE(0 == lst_test.sz_cur);
        REQUIRE(NULL == lst_test.list);
    }
    SECTION("test init func") {
        BRDLST_DECLARE_INIT(lst_test);
        REQUIRE(0 == lst_test.sz_max);
        REQUIRE(0 == lst_test.sz_cur);
        REQUIRE(NULL == lst_test.list);

        lst_test.sz_max = 1;
        lst_test.sz_cur = 2;
        lst_test.list = 3;
        brdlst_init(&lst_test);
        REQUIRE(0 == lst_test.sz_max);
        REQUIRE(0 == lst_test.sz_cur);
        REQUIRE(NULL == lst_test.list);

        brdlst_clean(&lst_test);
        REQUIRE(0 == lst_test.sz_max);
        REQUIRE(0 == lst_test.sz_cur);
        REQUIRE(NULL == lst_test.list);
    }
}
#endif /* CIUT_ENABLED */


#define MY_ISSPACE(a) (isblank(a) || ('\n' == (a)) || ('\r' == (a)))
#define MY_DATA_SPLIT(a) ('\t' == (a))

/**
 * \brief fetch the column of a line, separeted by '\t'
 * \param line: the record line
 * \param column: the index of column, start from 0
 * \param ret_buf: the buffer to store the result
 * \param sz_buf: the size of the buffer
 * \return 0 on success, <0 on failed
 */
int
cstr_line_get_column(const char * line, int column, char * ret_buf, size_t sz_buf)
{
    char * pstart = NULL;
    char * pstrend = NULL;
    char * pend = NULL;
    int column_cur = 0;

    pstrend = (char *)line + strlen(line);
    for (column_cur = 0; (column_cur < column) && (pstart < pstrend); column_cur ++) {
        for(pend = (char *)line; *pend && (! MY_DATA_SPLIT(*pend)) && (pend < pstrend); pend ++);
        if (! MY_DATA_SPLIT(*pend)) {
            break;
        }
        line = pend + 1;
    }
    if (column_cur < column) {
        // not found
        return -1;
    }
    for(pstart = (char *)line; *pstart && MY_ISSPACE(*pstart) && pstart < pstrend; pstart ++);
    for(pend = pstart; *pend && (! MY_DATA_SPLIT(*pend)) && (pend < pstrend); pend ++);
    for(pend --; *pend && MY_ISSPACE(*pend) && pend > pstart; pend --);
    pend ++;
    if (pend - pstart > sz_buf) {
        return -1;
    }
    memmove (ret_buf, pstart, pend - pstart);
    ret_buf[pend - pstart] = 0;
    return 0;
}

#if defined(CIUT_ENABLED) && (CIUT_ENABLED == 1)
#include <ciut.h>

TEST_CASE( .description="test cstr_line_get_column.", .skip=0 ) {

    SECTION("test cstr_line_get_column") {
        char buf[100] = "";
        //CIUT_LOG ("atten invalid arg");
#define CSTR_TEST "  a\t  b  \t c\t d \n"
        REQUIRE(0 == cstr_line_get_column(CSTR_TEST, 0,  buf, sizeof(buf)));
        CIUT_LOG ("cstr_line_get_column() got '%s'", buf);
        REQUIRE(0 == strcmp("a", buf));
        REQUIRE(0 == cstr_line_get_column(CSTR_TEST, 1,  buf, sizeof(buf)));
        REQUIRE(0 == strcmp("b", buf));
        REQUIRE(0 == cstr_line_get_column(CSTR_TEST, 2,  buf, sizeof(buf)));
        REQUIRE(0 == strcmp("c", buf));
        REQUIRE(0 == cstr_line_get_column(CSTR_TEST, 3,  buf, sizeof(buf)));
        REQUIRE(0 == strcmp("d", buf));
        REQUIRE(-1 == cstr_line_get_column(CSTR_TEST, 4,  buf, sizeof(buf)));
#undef CSTR_TEST
    }
}
#endif /* CIUT_ENABLED */


BRDLST_DECLARE_INIT(g_lst_brd);
BRDLST_DECLARE_INIT(g_lst_edio24);

/**
 * \brief process a line of a file
 * \param pos: the position of the line in the file
 * \param buf: the data of the line
 * \param size: the size of the buffer
 * \param userdata: the argument passed by user
 * \return 0 on success, <0 on failed
 */
int
cb_process_brdlst_line (off_t pos, char * buf, size_t size, void *userdata)
{
    board_list_t *plst = (board_list_t *)userdata;
    assert (NULL != userdata);

    //fprintf(stdout, "process edio24 line: '%s'\n", buf);
    // strip the string
    if (0 != cstr_strip (buf, buf, size)) {
        fprintf(stderr, "error in process line: '%s'\n", buf);
        return -1;
    }
    // if the line is comment line
    if ('#' == buf[0]) {
        fprintf(stderr, "skip line: '%s'\n", buf);
        return 0;
    }
    if (0 == buf[0]) {
        //fprintf(stderr, "skip line: '%s'\n", buf);
        return 0;
    }
    //fprintf(stderr, "start line: '%s'\n", buf);
    // if the line is invalid

    // add to the list
    return brdlst_append(plst, buf);
}

/**
 * \brief read file and store the data in a global variable for EDIO24
 * \param fn: the file name of the edio24 config
 */
static void
read_file_edio24(const char * fn)
{
    board_list_t *plst = &g_lst_edio24;
    brdlst_clean(plst);
    brdlst_init(plst);
    read_file_lines(fn, (void *)(plst), cb_process_brdlst_line);
}

/**
 * \brief read file and store the data in a global variable for boards
 * \param fn: the file name of the board config
 */
static void
read_file_board(const char * fn)
{
    board_list_t *plst = &g_lst_brd;
    brdlst_clean(plst);
    brdlst_init(plst);
    read_file_lines(fn, (void *)(plst), cb_process_brdlst_line);
}

#if defined(CIUT_ENABLED) && (CIUT_ENABLED == 1)
static int
create_test_file_edio24conf(const char * fn_test)
{
    FILE *fp = NULL;

    assert (NULL != fn_test);

    unlink(fn_test);

    fp = fopen(fn_test, "w+");
    if (NULL == fp) {
        fprintf(stderr, "Error in create file: '%s'\n", fn_test);
        return -1;
    }
    fprintf(fp, "192.168.1.101	00:11:22:33:44:01	E-DIO24-334401" "\n");
    fprintf(fp, "192.168.1.102	00:11:22:33:44:02	E-DIO24-334402" "\n");
    fprintf(fp, "192.168.1.103	00:11:22:33:44:03	E-DIO24-334403" "\n");
    fprintf(fp, "192.168.1.104	00:11:22:33:44:04	E-DIO24-334404" "\n");
    fprintf(fp, "192.168.1.105	00:11:22:33:44:05	E-DIO24-334405" "\n");
    fprintf(fp, "192.168.1.106	00:11:22:33:44:06	E-DIO24-334406" "\n");
    fprintf(fp, "192.168.1.107	00:11:22:33:44:07	E-DIO24-334407" "\n");
    fprintf(fp, "192.168.1.108	00:11:22:33:44:08	E-DIO24-334408" "\n");
    fprintf(fp, "192.168.1.109	00:11:22:33:44:09	E-DIO24-334409" "\n");
    fprintf(fp, "192.168.1.110	00:11:22:33:44:10	E-DIO24-334410" "\n");
    fprintf(fp, "192.168.1.111	00:11:22:33:44:11	E-DIO24-334411" "\n");
    fprintf(fp, "192.168.1.112	00:11:22:33:44:12	E-DIO24-334412" "\n");
    fprintf(fp, "192.168.1.113	00:11:22:33:44:13	E-DIO24-334413" "\n");
    fprintf(fp, "192.168.1.114	00:11:22:33:44:14	E-DIO24-334414" "\n");
    fprintf(fp, "192.168.1.115	00:11:22:33:44:15	E-DIO24-334415" "\n");
    fprintf(fp, "192.168.1.116	00:11:22:33:44:16	E-DIO24-334416" "\n");

    fclose(fp);
    return 0;
}

static int
create_test_file_boardsconf(const char * fn_test)
{
    FILE *fp = NULL;

    assert (NULL != fn_test);

    unlink(fn_test);

    fp = fopen(fn_test, "w+");
    if (NULL == fp) {
        fprintf(stderr, "Error in create file: '%s'\n", fn_test);
        return -1;
    }
    fprintf(fp, "Beside CNT	112233359	1029	998877665506F505	3344556679CF	192.168.1.153	fe80:cb:0:b062::xx	fe80:cb:0:b088::xx" "\n");
    fprintf(fp, "CCT_C01-01	112233479	707	99887766558D6DA5	33445566752C	192.168.1.19	fe80:cb:0:b062::7e10	fe80:cb:0:b088::29d4	" "\n");
    fprintf(fp, "CCT_C01-02	112233498	717	99887766558D6DB8	334455667518	192.168.1.89	fe80:cb:0:b062::5c36	fe80:cb:0:b088::3ee4	" "\n");
    fprintf(fp, "CCT_C01-03	112233480	718	99887766558D6DA6	33445566752B	192.168.1.28	fe80:cb:0:b062::4b6c	fe80:cb:0:b088::5912	" "\n");
    fprintf(fp, "CCT_C01-04	112233476	720	99887766558D6DA2	33445566752F	192.168.1.11	fe80:cb:0:b062::9222	fe80:cb:0:b088::be84	" "\n");
    fprintf(fp, "CCT_C01-05	112233551	708	99887766558D6DED	3344556674E3	192.168.1.18	fe80:cb:0:b062::a970	fe80:cb:0:b088::b1e6	" "\n");

    fclose(fp);
    return 0;
}
#define FN_CONF_EDIO24 "tmp-conf-edio24.txt"
#define FN_CONF_BOARDS "tmp-conf-boards.txt"
#endif /* CIUT_ENABLED */

#if defined(CIUT_ENABLED) && (CIUT_ENABLED == 1)
#include <ciut.h>

TEST_CASE( .name="read-conf", .description="test read_file_xxx.", .skip=0 ) {

    board_list_t *plst = &g_lst_brd;

    SECTION("test read_file_edio24") {
        unlink(FN_CONF_EDIO24);
        REQUIRE(0 == create_test_file_edio24conf(FN_CONF_EDIO24));
        plst = &g_lst_edio24;
        brdlst_clean(plst);
        brdlst_init(plst);
        read_file_edio24(FN_CONF_EDIO24);
        REQUIRE(16 == brdlst_length(plst));
        read_file_edio24(FN_CONF_EDIO24);
        REQUIRE(16 == brdlst_length(plst));
    }

    SECTION("test read_file_board") {
        unlink(FN_CONF_BOARDS);
        REQUIRE(0 == create_test_file_boardsconf(FN_CONF_BOARDS));

        plst = &g_lst_brd;
        brdlst_clean(plst);
        brdlst_init(plst);
        read_file_board(FN_CONF_BOARDS);
        REQUIRE(6 == brdlst_length(plst));
        read_file_board(FN_CONF_BOARDS);
        REQUIRE(6 == brdlst_length(plst));
    }
}

#endif /* CIUT_ENABLED */


/**
 * \brief get the IP section of a EDIO24 record
 * \param id: the index of the record
 * \param ret_buf: the buffer to store the result
 * \param sz_buf: the size of the buffer
 * \return 0 on success, <0 on fail
 */
int
get_ip_of_edio24(int id, char * ret_buf, size_t sz_buf)
{
    char * line;

    line = brdlst_get(&g_lst_edio24, id);
    if (NULL == line) {
        return -1;
    }
    // find the first column
    assert (NULL != line);
    return cstr_line_get_column (line, 0, ret_buf, sz_buf);
}

/**
 * \brief get the name section of a EDIO24 record
 * \param id: the index of the record
 * \param ret_buf: the buffer to store the result
 * \param sz_buf: the size of the buffer
 * \return 0 on success, <0 on fail
 */
int
get_name_of_edio24(int id, char * ret_buf, size_t sz_buf)
{
    char * line;

    line = brdlst_get(&g_lst_edio24, id);
    if (NULL == line) {
        return -1;
    }
    assert (NULL != line);
    return cstr_line_get_column (line, 2, ret_buf, sz_buf);
}

/**
 * \brief get the MAC section of a EDIO24 record
 * \param id: the index of the record
 * \param ret_buf: the buffer to store the result
 * \param sz_buf: the size of the buffer
 * \return 0 on success, <0 on fail
 */
int
get_mac_of_edio24(int id, char * ret_buf, size_t sz_buf)
{
    char * line;

    line = brdlst_get(&g_lst_edio24, id);
    if (NULL == line) {
        return -1;
    }
    assert (NULL != line);
    return cstr_line_get_column (line, 1, ret_buf, sz_buf);
}

/*"data_list[idx] - *data_pin"*/
int
pf_bsearch_cb_comp_brdlst_board_name(void *userdata, size_t idx, void * data_pin)
{
    int ret;
    char * line = NULL;
    char buf[100] = "";
    board_list_t *plst = (board_list_t *)userdata;
    assert (NULL != plst);
    assert (NULL != data_pin);

    assert ((0 <= idx) && (idx < plst->sz_cur));
    line = brdlst_get(plst, idx);
    assert (NULL != line);
    ret = cstr_line_get_column (line, 0, buf, sizeof(buf));
    assert (ret >= 0);
    return strcmp(buf, (const char *)data_pin);
}

/**
 * \brief get the IP section of a board record
 * \param name_id: the name of the record
 * \param ret_buf: the buffer to store the result
 * \param sz_buf: the size of the buffer
 * \return 0 on success, <0 on fail
 */
int
get_ip_of_board(const char *name_id, char * ret_buf, size_t sz_buf)
{
    int ret;
    char * line = NULL;
    size_t idx = 0;
    board_list_t *plst = &g_lst_brd;

    ret = pf_bsearch_r((void *)plst, brdlst_length(plst), pf_bsearch_cb_comp_brdlst_board_name, (void *)name_id, &idx);
    if (ret < 0) {
        return -1;
    }
    line = brdlst_get(plst, idx);
    if (NULL == line) {
        return -1;
    }
    // find the first column
    assert (NULL != line);
    return cstr_line_get_column (line, 5, ret_buf, sz_buf);
}

/**
 * \brief get the MAC section of a board record
 * \param name_id: the name of the record
 * \param ret_buf: the buffer to store the result
 * \param sz_buf: the size of the buffer
 * \return 0 on success, <0 on fail
 */
int
get_mac_of_board(const char *name_id, char * ret_buf, size_t sz_buf)
{
    int ret;
    char * line = NULL;
    size_t idx = 0;
    board_list_t *plst = &g_lst_brd;

    ret = pf_bsearch_r((void *)plst, brdlst_length(plst), pf_bsearch_cb_comp_brdlst_board_name, (void *)name_id, &idx);
    if (ret < 0) {
        return -1;
    }
    line = brdlst_get(plst, idx);
    if (NULL == line) {
        return -1;
    }
    // find the first column
    assert (NULL != line);
    return cstr_line_get_column (line, 4, ret_buf, sz_buf);
}

/**
 * \brief get the NGCMAC section of a board record
 * \param name_id: the name of the record
 * \param ret_buf: the buffer to store the result
 * \param sz_buf: the size of the buffer
 * \return 0 on success, <0 on fail
 */
int
get_ngcmac_of_board(const char *name_id, char * ret_buf, size_t sz_buf)
{
    int ret;
    char * line = NULL;
    size_t idx = 0;
    board_list_t *plst = &g_lst_brd;

    ret = pf_bsearch_r((void *)plst, brdlst_length(plst), pf_bsearch_cb_comp_brdlst_board_name, (void *)name_id, &idx);
    if (ret < 0) {
        return -1;
    }
    line = brdlst_get(plst, idx);
    if (NULL == line) {
        return -1;
    }
    // find the first column
    assert (NULL != line);
    return cstr_line_get_column (line, 3, ret_buf, sz_buf);
}

#if defined(CIUT_ENABLED) && (CIUT_ENABLED == 1)
#include <ciut.h>

TEST_CASE( .description="test get_ip_of_board.", .skip=0 ) {
    int ret;
    char buf[100] = "";

    SECTION("test parameters") {
        board_list_t *plst = &g_lst_brd;

        brdlst_clean(plst);
        brdlst_init(plst);

        ret = get_ip_of_board(NULL, NULL, 0);
        REQUIRE(-1 == ret);
        ret = get_ip_of_board(NULL, NULL, sizeof(buf));
        REQUIRE(-1 == ret);
        ret = get_ip_of_board(NULL, buf, sizeof(buf));
        REQUIRE(-1 == ret);
        ret = get_ip_of_board("", buf, sizeof(buf));
        REQUIRE(-1 == ret);
        ret = get_ip_of_board("", NULL, sizeof(buf));
        REQUIRE(-1 == ret);
        ret = get_ip_of_board("", NULL, 0);
        REQUIRE(-1 == ret);
        ret = get_ip_of_board("CCT_C01-01", buf, sizeof(buf));
        REQUIRE(-1 == ret);

        brdlst_clean(plst);
        brdlst_init(plst);
    }
    SECTION("test board data") {
        board_list_t *plst = &g_lst_brd;

        brdlst_clean(plst);
        brdlst_init(plst);

        //CIUT_LOG ("insert data");
        //CIUT_LOG ("#Locataion	Serial_Nbr	InvId	NGC_MAC 	WIFI_MAC	WiFi_address	IPv6_Address	IPv6_Address_Alt	");
        brdlst_append(plst, "Beside CNT	112233359	1029	998877665506F505	3344556679CF	192.168.1.153	fe80:cb:0:b062::xx	fe80:cb:0:b088::xx");
        brdlst_append(plst, "CCT_C01-01	112233479	707	99887766558D6DA5	33445566752C	192.168.1.19	fe80:cb:0:b062::7e10	fe80:cb:0:b088::29d4	");
        brdlst_append(plst, "CCT_C01-02	112233498	717	99887766558D6DB8	334455667518	192.168.1.89	fe80:cb:0:b062::5c36	fe80:cb:0:b088::3ee4	");
        brdlst_append(plst, "CCT_C01-03	112233480	718	99887766558D6DA6	33445566752B	192.168.1.28	fe80:cb:0:b062::4b6c	fe80:cb:0:b088::5912	");
        brdlst_append(plst, "CCT_C01-04	112233476	720	99887766558D6DA2	33445566752F	192.168.1.11	fe80:cb:0:b062::9222	fe80:cb:0:b088::be84	");
        brdlst_append(plst, "CCT_C01-05	112233551	708	99887766558D6DED	3344556674E3	192.168.1.18	fe80:cb:0:b062::a970	fe80:cb:0:b088::b1e6	");

        ret = get_ip_of_board("CCT_C01-06", buf, sizeof(buf));
        REQUIRE(-1 == ret);
        ret = get_ip_of_board("", buf, sizeof(buf));
        REQUIRE(-1 == ret);
        ret = get_ip_of_board("", NULL, sizeof(buf));
        REQUIRE(-1 == ret);
        ret = get_ip_of_board("", NULL, 0);
        REQUIRE(-1 == ret);

        //CIUT_LOG ("find the data");
        ret = get_ip_of_board("Beside CNT", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("192.168.1.153", buf));
        ret = get_mac_of_board("Beside CNT", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("3344556679CF", buf));
        ret = get_ngcmac_of_board("Beside CNT", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("998877665506F505", buf));

        ret = get_ip_of_board("CCT_C01-01", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("192.168.1.19", buf));
        ret = get_mac_of_board("CCT_C01-01", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("33445566752C", buf));
        ret = get_ngcmac_of_board("CCT_C01-01", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("99887766558D6DA5", buf));

        ret = get_ip_of_board("CCT_C01-03", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("192.168.1.28", buf));
        ret = get_mac_of_board("CCT_C01-03", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("33445566752B", buf));
        ret = get_ngcmac_of_board("CCT_C01-03", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("99887766558D6DA6", buf));

        ret = get_ip_of_board("CCT_C01-05", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("192.168.1.18", buf));
        ret = get_mac_of_board("CCT_C01-05", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("3344556674E3", buf));
        ret = get_ngcmac_of_board("CCT_C01-05", buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("99887766558D6DED", buf));

        brdlst_clean(plst);
        brdlst_init(plst);
    }
    SECTION("test edio24 data") {
        board_list_t *plst = &g_lst_edio24;

        brdlst_clean(plst);
        brdlst_init(plst);

        //CIUT_LOG ("insert data");
        brdlst_append(plst, "192.168.1.101	00:11:22:33:44:01	E-DIO24-334401");
        brdlst_append(plst, "192.168.1.102	00:11:22:33:44:02	E-DIO24-334402");
        brdlst_append(plst, "192.168.1.103	00:11:22:33:44:03	E-DIO24-334403");
        brdlst_append(plst, "192.168.1.104	00:11:22:33:44:04	E-DIO24-334404");
        brdlst_append(plst, "192.168.1.105	00:11:22:33:44:05	E-DIO24-334405");
        brdlst_append(plst, "192.168.1.106	00:11:22:33:44:06	E-DIO24-334406");
        brdlst_append(plst, "192.168.1.107	00:11:22:33:44:07	E-DIO24-334407");
        brdlst_append(plst, "192.168.1.108	00:11:22:33:44:08	E-DIO24-334408");
        brdlst_append(plst, "192.168.1.109	00:11:22:33:44:09	E-DIO24-334409");
        brdlst_append(plst, "192.168.1.110	00:11:22:33:44:10	E-DIO24-334410");
        brdlst_append(plst, "192.168.1.111	00:11:22:33:44:11	E-DIO24-334411");
        brdlst_append(plst, "192.168.1.112	00:11:22:33:44:12	E-DIO24-334412");
        brdlst_append(plst, "192.168.1.113	00:11:22:33:44:13	E-DIO24-334413");
        brdlst_append(plst, "192.168.1.114	00:11:22:33:44:14	E-DIO24-334414");
        brdlst_append(plst, "192.168.1.115	00:11:22:33:44:15	E-DIO24-334415");
        brdlst_append(plst, "192.168.1.116	00:11:22:33:44:16	E-DIO24-334416");

        ret = get_ip_of_edio24(0, buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("192.168.1.101", buf));
        ret = get_name_of_edio24(0, buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("E-DIO24-334401", buf));
        ret = get_mac_of_edio24(0, buf, sizeof(buf));
        REQUIRE(0 == ret);
        REQUIRE(0 ==  strcmp("00:11:22:33:44:01", buf));

        brdlst_clean(plst);
        brdlst_init(plst);
    }
}
#endif /* CIUT_ENABLED */

/*****************************************************************************/
/**
 * \brief get the IP of boards
 * \param fp: the FILE pointer for output
 * \param arg_cluster: the C string for cluster info
 * \param arg_cmd: the C string for board info
 */
void
do_get_ip_board(FILE *fp, const char * arg_cluster, const char *arg_cmd)
{
    int i;
    int j;
    int ret;
    unsigned int val;
    unsigned int mask_c;
    unsigned int mask_p;
    char buf[200];

    if (NULL == arg_cluster) {
        arg_cluster = "";
    }
    val = 0;
    parse_values_atten (arg_cluster, &mask_c, &val);

    if (NULL == arg_cmd) {
        arg_cmd = "";
    }
    val = 0;
    parse_values_atten (arg_cmd, &mask_p, &val);

    for (i = 0; i <= 16; i ++) {
        if (mask_c & (0x0001 << i)) {
            for (j = 0; j <= 8; j ++) {
                if (mask_p & (0x0001 << j)) {
                    snprintf(buf, sizeof(buf) - 1, "CCT_C%02d-%02d", i, j);
                    ret = get_ip_of_board(buf, buf, sizeof(buf));
                    if (ret >= 0) {
                        fprintf (fp, "CCT_C%02d-%02d\t%s", i, j, buf);
                    }
                }
            }
        }
    }
}

/**
 * \brief get the record rows of boards
 * \param fp: the FILE pointer for output
 * \param arg_cluster: the C string for cluster info
 */
void
do_get_all_board(FILE *fp, const char * arg_cluster, const char *arg_cmd)
{
    int i;
    int j;
    unsigned int val = EDIO24_INVALID_INT;
    unsigned int mask_c;
    unsigned int mask_p;
    char buf[200];

    if (NULL == arg_cmd) {
        arg_cmd = "";
    }
    val = EDIO24_INVALID_INT;
    parse_values_atten (arg_cmd, &mask_p, &val);
    if (EDIO24_INVALID_INT != val) {
        fprintf(stderr, "Error: set the forbidden value\n");
        return;
    }

    if (NULL == arg_cluster) {
        arg_cluster = "";
    }
    val = EDIO24_INVALID_INT;
    parse_values_atten (arg_cluster, &mask_c, &val);
    if (EDIO24_INVALID_INT != val) {
        fprintf(stderr, "Error: set the forbidden value\n");
        return;
    }

    for (i = 0; i <= 16; i ++) {
        if (mask_c & (0x0001 << i)) {
            for (j = 0; j <= 8; j ++) {
                if (mask_p & (0x0001 << j)) {

                    int ret;
                    char * line = NULL;
                    size_t idx = 0;
                    board_list_t *plst = &g_lst_brd;
                    assert (NULL != plst);
                    assert (0 < brdlst_length(plst));

                    snprintf(buf, sizeof(buf) - 1, "CCT_C%02d-%02d", i, j);

                    ret = pf_bsearch_r(plst, brdlst_length(plst), pf_bsearch_cb_comp_brdlst_board_name, buf, &idx);
                    if (ret < 0) {
                        continue;
                    }
                    line = brdlst_get(plst, idx);
                    if (NULL == line) {
                        continue;
                    }

                    fprintf (fp, "%s\n", line);
                }
            }
        }
    }
}

#if defined(CIUT_ENABLED) && (CIUT_ENABLED == 1)
#include <ciut.h>

typedef struct _ciut_test_getboards_t {
    char * arg_cluster;
    const char *arg_cmd;
} ciut_test_getboards_t;

static int
cb_cuit_check_output_getbrdip (FILE * outf, void * user_arg)
{
    ciut_test_getboards_t * arg = (ciut_test_getboards_t *)user_arg;
    do_get_ip_board(outf, arg->arg_cluster, arg->arg_cmd);
    return 0;
}

static int
cb_cuit_check_output_getboards (FILE * outf, void * user_arg)
{
    ciut_test_getboards_t * arg = (ciut_test_getboards_t *)user_arg;
    do_get_all_board(outf, arg->arg_cluster, arg->arg_cmd);
    return 0;
}

TEST_CASE( .name="get-boards", .description="test get boards.", .skip=0 ) {
    int i;
    ciut_test_getboards_t args;

    static const char * iodata_boards[] = {
        // cluster, position, output
        "p=1", "p=1",   "CCT_C01-01	112233479	707	99887766558D6DA5	33445566752C	192.168.1.19	fe80:cb:0:b062::7e10	fe80:cb:0:b088::29d4\n",
        "p=1", "p=2",   "CCT_C01-02	112233498	717	99887766558D6DB8	334455667518	192.168.1.89	fe80:cb:0:b062::5c36	fe80:cb:0:b088::3ee4\n",
    };

    static const char * iodata_boardsip[] = {
        // cluster, position, output
        "p=1", "p=1",   "CCT_C01-01	192.168.1.19",
        "p=1", "p=2",   "CCT_C01-02	192.168.1.89",
    };
    board_list_t *plst = &g_lst_brd;
    unlink(FN_CONF_EDIO24);
    REQUIRE(0 == create_test_file_edio24conf(FN_CONF_EDIO24));
    plst = &g_lst_edio24;
    read_file_edio24(FN_CONF_EDIO24);
    REQUIRE(16 == brdlst_length(plst));

    unlink(FN_CONF_BOARDS);
    REQUIRE(0 == create_test_file_boardsconf(FN_CONF_BOARDS));
    plst = &g_lst_brd;
    read_file_board(FN_CONF_BOARDS);
    REQUIRE(6 == brdlst_length(plst));

    SECTION("test output get-boards") {
        for (i = 0; i < ARRAY_NUM(iodata_boards); i += 3) {
            CIUT_LOG ("board test idx=%d", i/3);
            args.arg_cluster = iodata_boards[i + 0];
            args.arg_cmd = iodata_boards[i + 1];
            REQUIRE(0 == cuit_check_output(cb_cuit_check_output_getboards, &args, iodata_boards[i + 2]));
        }
        for (i = 0; i < ARRAY_NUM(iodata_boardsip); i += 3) {
            CIUT_LOG ("board test idx=%d", i/3);
            args.arg_cluster = iodata_boardsip[i + 0];
            args.arg_cmd = iodata_boardsip[i + 1];
            REQUIRE(0 == cuit_check_output(cb_cuit_check_output_getbrdip, &args, iodata_boardsip[i + 2]));
        }
    }
}
#endif /* CIUT_ENABLED */

/**
 * \brief get the EDIO24 raw commands sequence to set attenuations
 * \param fp: the FILE pointer for output
 * \param arg_cluster: the C string for cluster info
 * \param arg_cmd: the C string for board info
 */
void
do_setatten(FILE *fp, const char * arg_cluster, const char *arg_cmd)
{
    int i;
    int j;
    int ret;
    unsigned int val;
    unsigned int val1;
    unsigned int val2;
    unsigned int mask_c;
    unsigned int mask_p;
    char buf[200];

    if (NULL == arg_cluster) {
        arg_cluster = "";
    }
    val = 0;
    parse_values_atten (arg_cluster, &mask_c, &val);
    mask_c &= 0x1FFFF;

    if (NULL == arg_cmd) {
        arg_cmd = "";
    }
    val = 0;
    parse_values_atten (arg_cmd, &mask_p, &val);
    mask_p &= 0x03;
    {
        val1 = val / 2;
        //fprintf(stderr, "do_setatten: 1 val=%d, val1=%d\n", val, val1);
        if (0 == val1) {
            val1 = 0;
            val2 = val;
        } else {
            val2 = val - val1;
        }
        //fprintf(stderr, "do_setatten: 2 val=%d, val1=%d, val2=%d\n", val, val1, val2);
    }
    //fprintf(stderr, "do_setatten: arg_cluster='%s', arg_cmd='%s'; mask_p=0x%02X, val=%d\n", arg_cluster, arg_cmd, mask_p, val);

    // skip the cluster 0 (root)
    for (i = 1; i <= 16; i ++) {
        //fprintf(stderr, "do_setatten: cluster i=%d\n", i);
        if (mask_c & (0x0001 << i)) {
            ret = get_ip_of_edio24(i-1, buf, sizeof(buf));
            if (ret < 0) {
                fprintf(stderr, "error in get the ip of edio24 at idx=%d\n", i);
                continue;
            }
            fprintf(fp, "# ConnectTo %s # %d\n", brdlst_get(&g_lst_edio24, i-1), i);
            assert (val == val1 + val2);
            if (val1 == val2) {
                char *p = buf;
                snprintf(buf, sizeof(buf)-1, "v=%d;p=", val1);
                p = buf + strlen(buf);
                assert(p < buf + sizeof(buf));
                j = 0;
                if (mask_p & (0x0001 << j)) {
                    //fprintf(stderr, "mask_p active at idx=%d\n", j);
                    snprintf(p, sizeof(buf)-1-(p-buf), "%d,%d", j*2, j*2+1);
                }
                p = buf + strlen(buf);
                assert(p < buf + sizeof(buf));
                j = 1;
                if (mask_p & (0x0001 << j)) {
                    //fprintf(stderr, "mask_p active at idx=%d\n", j);
                    snprintf(p, sizeof(buf)-1-(p-buf), ",%d,%d", j*2, j*2+1);
                }
                p = buf + strlen(buf);
                assert(p < buf + sizeof(buf));
                output_atten(fp, buf);
            } else {
                char *p = buf;
                snprintf(buf, sizeof(buf)-1, "v=%d;p=", val1);
                p = buf + strlen(buf);
                assert(p < buf + sizeof(buf));
                j = 0;
                if (mask_p & (0x0001 << j)) {
                    //fprintf(stderr, "2 mask_p active at idx=%d\n", j);
                    snprintf(p, sizeof(buf)-1-(p-buf), "%d", j*2);
                }
                p = buf + strlen(buf);
                assert(p < buf + sizeof(buf));
                j = 1;
                if (mask_p & (0x0001 << j)) {
                    //fprintf(stderr, "2 mask_p active at idx=%d\n", j);
                    snprintf(p, sizeof(buf)-1-(p-buf), ",%d", j*2);
                }
                output_atten(fp, buf);
                snprintf(buf, sizeof(buf)-1, "v=%d;p=", val2);
                p = buf + strlen(buf);
                assert(p < buf + sizeof(buf));
                j = 0;
                if (mask_p & (0x0001 << j)) {
                    //fprintf(stderr, "2 mask_p active at idx=%d\n", j);
                    snprintf(p, sizeof(buf)-1-(p-buf), "%d", j*2+1);
                }
                p = buf + strlen(buf);
                assert(p < buf + sizeof(buf));
                j = 1;
                if (mask_p & (0x0001 << j)) {
                    //fprintf(stderr, "2 mask_p active at idx=%d\n", j);
                    snprintf(p, sizeof(buf)-1-(p-buf), ",%d", j*2+1);
                }
                p = buf + strlen(buf);
                assert(p < buf + sizeof(buf));
                output_atten(fp, buf);
            }

        }
    }
}

/**
 * \brief get the EDIO24 raw commands sequence to set power on/off
 * \param fp: the FILE pointer for output
 * \param arg_cluster: the C string for cluster info
 * \param arg_cmd: the C string for board info
 */
void
do_setpower(FILE *fp, const char * arg_cluster, const char *arg_cmd)
{
    int i;
    int j;
    int ret;
    unsigned int val;
    unsigned int mask_c;
    unsigned int mask_p;
    char buf[200];

    if (NULL == arg_cluster) {
        arg_cluster = "";
    }
    val = 0;
    parse_values_atten (arg_cluster, &mask_c, &val);
    mask_c &= 0x1FFFF;

    if (NULL == arg_cmd) {
        arg_cmd = "";
    }
    val = 0;
    parse_values_atten (arg_cmd, &mask_p, &val);
    mask_p &= 0x01FF;
    mask_p >>= 1; // remove the position 0(for root)
    fprintf(stderr, "do_setpower: arg_cluster='%s', arg_cmd='%s'; mask_p=0x%02X, val=%d\n", arg_cluster, arg_cmd, mask_p, val);

    // skip the cluster 0 (root)
    for (i = 1; i <= 16; i ++) {
        if (mask_c & (0x0001 << i)) {
            ret = get_ip_of_edio24(i-1, buf, sizeof(buf));
            if (ret < 0) {
                fprintf(stderr, "error in get the ip of edio24 at idx=%d\n", i);
                continue;
            }
            fprintf(fp, "# ConnectTo %s # %d\n", brdlst_get(&g_lst_edio24, i-1), i);
            snprintf(buf, sizeof(buf)-1, "p=");
            for(j = 0; j < 8; j++) {
                if (mask_p & (0x0001 << j)) {
                    snprintf(buf+strlen(buf), sizeof(buf)-1-strlen(buf), "%d,", j);
                }
            }
            output_power(fp, buf);
        }
    }
}

/**
 * \brief get the EDIO24 raw commands sequence to get power status
 * \param fp: the FILE pointer for output
 * \param arg_cluster: the C string for cluster info
 * \param arg_cmd: the C string for board info
 */
void
do_getpower(FILE *fp, const char * arg_cluster, const char *arg_cmd)
{
    int i;
    int ret;
    unsigned int val;
    unsigned int mask_c;
    unsigned int mask_p;
    char buf[200];

    if (NULL == arg_cluster) {
        arg_cluster = "";
    }
    val = 0;
    parse_values_atten (arg_cluster, &mask_c, &val);
    mask_c &= 0x1FFFF;

    if (NULL == arg_cmd) {
        arg_cmd = "";
    }
    val = 0;
    parse_values_atten (arg_cmd, &mask_p, &val);
    mask_p &= 0x01FF;
    mask_p >>= 1; // remove the position 0(for root)
    fprintf(stderr, "do_getpower: arg_cluster='%s', arg_cmd='%s'; mask_p=0x%02X, mask_c=0x%02X, val=%d\n", arg_cluster, arg_cmd, mask_p, mask_c, val);

    // skip the cluster 0 (root)
    for (i = 1; i <= 16; i ++) {
        if (mask_c & (0x0001 << i)) {
            ret = get_ip_of_edio24(i-1, buf, sizeof(buf));
            if (ret < 0) {
                fprintf(stderr, "error in get the ip of edio24 at idx=%d\n", i);
                continue;
            }
            fprintf(fp, "# ConnectTo %s # %d\n", brdlst_get(&g_lst_edio24, i-1), i);
            output_power_read(fp, (void *)arg_cmd);
        }
    }
}

#if defined(CIUT_ENABLED) && (CIUT_ENABLED == 1)

#include <ciut.h>

typedef struct _ciut_test_dotest_t {
    char * arg_cluster;
    const char *arg_cmd;
} ciut_test_dotest_t;

static int
cb_cuit_check_output_doatten (FILE * outf, void * user_arg)
{
    ciut_test_dotest_t * arg = (ciut_test_dotest_t *)user_arg;
    do_setatten(outf, arg->arg_cluster, arg->arg_cmd);
    return 0;
}

static int
cb_cuit_check_output_dosetpower (FILE * outf, void * user_arg)
{
    ciut_test_dotest_t * arg = (ciut_test_dotest_t *)user_arg;
    do_setpower(outf, arg->arg_cluster, arg->arg_cmd);
    return 0;
}

static int
cb_cuit_check_output_dogetpower (FILE * outf, void * user_arg)
{
    ciut_test_dotest_t * arg = (ciut_test_dotest_t *)user_arg;
    do_getpower(outf, arg->arg_cluster, arg->arg_cmd);
    return 0;
}

TEST_CASE( .name="do-test", .description="test output cluster.", .skip=0 ) {

    int i;
    ciut_test_dotest_t args;

    static const char * iodata_atten[] = {
        // cluster, atten pos, output
        "p=1", "p=all;v=20",   "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDConfigW 0xFFFFFF 0x000000\nDOutW 0x000F00 0x000000\nDOutW 0xFF0000 0x0A0000\nSleep 1\nDOutW 0x000F00 0x000F00\nSleep 1\nDOutW 0x000F00 0x000000\nDOutW 0xFF0000 0x000000\n\n",
        "p=1", "p=all;v=31",   "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDConfigW 0xFFFFFF 0x000000\nDOutW 0x000500 0x000000\nDOutW 0xFF0000 0x0F0000\nSleep 1\nDOutW 0x000500 0x000500\nSleep 1\nDOutW 0x000500 0x000000\nDOutW 0xFF0000 0x000000\n\nDConfigW 0xFFFFFF 0x000000\nDOutW 0x000A00 0x000000\nDOutW 0xFF0000 0x100000\nSleep 1\nDOutW 0x000A00 0x000A00\nSleep 1\nDOutW 0x000A00 0x000000\nDOutW 0xFF0000 0x000000\n\n",
        "p=1", "p=0;v=31",     "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDConfigW 0xFFFFFF 0x000000\nDOutW 0x000100 0x000000\nDOutW 0xFF0000 0x0F0000\nSleep 1\nDOutW 0x000100 0x000100\nSleep 1\nDOutW 0x000100 0x000000\nDOutW 0xFF0000 0x000000\n\nDConfigW 0xFFFFFF 0x000000\nDOutW 0x000200 0x000000\nDOutW 0xFF0000 0x100000\nSleep 1\nDOutW 0x000200 0x000200\nSleep 1\nDOutW 0x000200 0x000000\nDOutW 0xFF0000 0x000000\n\n",
    };
    static const char * iodata_power[] = {
        // cluster, board pos, output
        "p=1", "p=",       "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDConfigW 0xFFFFFF 0x000000\nDOutW 0x0000FF 0x000000\n\n",
        "p=1", "p=0,1,2",  "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDConfigW 0xFFFFFF 0x000000\nDOutW 0x0000FF 0x000003\n\n",
        "p=1", "p=1,2",    "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDConfigW 0xFFFFFF 0x000000\nDOutW 0x0000FF 0x000003\n\n",
        "p=1", "p=1,2,3",  "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDConfigW 0xFFFFFF 0x000000\nDOutW 0x0000FF 0x000007\n\n",
        "p=1", "p=all",    "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDConfigW 0xFFFFFF 0x000000\nDOutW 0x0000FF 0x0000FF\n\n",
    };
    static const char * iodata_power_read[] = {
        // cluster, board pos, output
        "p=1", "p=",       "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDOutR\n\n",
        "p=1", "p=0,1,2",  "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDOutR\n\n",
        "p=1", "p=all",    "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDOutR\n\n",
#if 1
        "p=0,1", "p=all",    "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDOutR\n\n",
        "p=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,15", "p=all",
            "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDOutR\n\n"
            "# ConnectTo 192.168.1.102	00:11:22:33:44:02	E-DIO24-334402 # 2\nDOutR\n\n"
            "# ConnectTo 192.168.1.103	00:11:22:33:44:03	E-DIO24-334403 # 3\nDOutR\n\n"
            "# ConnectTo 192.168.1.104	00:11:22:33:44:04	E-DIO24-334404 # 4\nDOutR\n\n"
            "# ConnectTo 192.168.1.105	00:11:22:33:44:05	E-DIO24-334405 # 5\nDOutR\n\n"
            "# ConnectTo 192.168.1.106	00:11:22:33:44:06	E-DIO24-334406 # 6\nDOutR\n\n"
            "# ConnectTo 192.168.1.107	00:11:22:33:44:07	E-DIO24-334407 # 7\nDOutR\n\n"
            "# ConnectTo 192.168.1.108	00:11:22:33:44:08	E-DIO24-334408 # 8\nDOutR\n\n"
            "# ConnectTo 192.168.1.109	00:11:22:33:44:09	E-DIO24-334409 # 9\nDOutR\n\n"
            "# ConnectTo 192.168.1.110	00:11:22:33:44:10	E-DIO24-334410 # 10\nDOutR\n\n"
            "# ConnectTo 192.168.1.111	00:11:22:33:44:11	E-DIO24-334411 # 11\nDOutR\n\n"
            "# ConnectTo 192.168.1.112	00:11:22:33:44:12	E-DIO24-334412 # 12\nDOutR\n\n"
            "# ConnectTo 192.168.1.113	00:11:22:33:44:13	E-DIO24-334413 # 13\nDOutR\n\n"
            "# ConnectTo 192.168.1.114	00:11:22:33:44:14	E-DIO24-334414 # 14\nDOutR\n\n"
            "# ConnectTo 192.168.1.115	00:11:22:33:44:15	E-DIO24-334415 # 15\nDOutR\n\n"
            ,
        "p=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16", "p=all",
            "# ConnectTo 192.168.1.101	00:11:22:33:44:01	E-DIO24-334401 # 1\nDOutR\n\n"
            "# ConnectTo 192.168.1.102	00:11:22:33:44:02	E-DIO24-334402 # 2\nDOutR\n\n"
            "# ConnectTo 192.168.1.103	00:11:22:33:44:03	E-DIO24-334403 # 3\nDOutR\n\n"
            "# ConnectTo 192.168.1.104	00:11:22:33:44:04	E-DIO24-334404 # 4\nDOutR\n\n"
            "# ConnectTo 192.168.1.105	00:11:22:33:44:05	E-DIO24-334405 # 5\nDOutR\n\n"
            "# ConnectTo 192.168.1.106	00:11:22:33:44:06	E-DIO24-334406 # 6\nDOutR\n\n"
            "# ConnectTo 192.168.1.107	00:11:22:33:44:07	E-DIO24-334407 # 7\nDOutR\n\n"
            "# ConnectTo 192.168.1.108	00:11:22:33:44:08	E-DIO24-334408 # 8\nDOutR\n\n"
            "# ConnectTo 192.168.1.109	00:11:22:33:44:09	E-DIO24-334409 # 9\nDOutR\n\n"
            "# ConnectTo 192.168.1.110	00:11:22:33:44:10	E-DIO24-334410 # 10\nDOutR\n\n"
            "# ConnectTo 192.168.1.111	00:11:22:33:44:11	E-DIO24-334411 # 11\nDOutR\n\n"
            "# ConnectTo 192.168.1.112	00:11:22:33:44:12	E-DIO24-334412 # 12\nDOutR\n\n"
            "# ConnectTo 192.168.1.113	00:11:22:33:44:13	E-DIO24-334413 # 13\nDOutR\n\n"
            "# ConnectTo 192.168.1.114	00:11:22:33:44:14	E-DIO24-334414 # 14\nDOutR\n\n"
            "# ConnectTo 192.168.1.115	00:11:22:33:44:15	E-DIO24-334415 # 15\nDOutR\n\n"
            "# ConnectTo 192.168.1.116	00:11:22:33:44:16	E-DIO24-334416 # 16\nDOutR\n\n"
            ,
#endif
    };

    board_list_t *plst = &g_lst_edio24;

    brdlst_clean(plst);
    brdlst_init(plst);

    //CIUT_LOG ("insert data");
    brdlst_append(plst, "192.168.1.101	00:11:22:33:44:01	E-DIO24-334401");
    brdlst_append(plst, "192.168.1.102	00:11:22:33:44:02	E-DIO24-334402");
    brdlst_append(plst, "192.168.1.103	00:11:22:33:44:03	E-DIO24-334403");
    brdlst_append(plst, "192.168.1.104	00:11:22:33:44:04	E-DIO24-334404");
    brdlst_append(plst, "192.168.1.105	00:11:22:33:44:05	E-DIO24-334405");
    brdlst_append(plst, "192.168.1.106	00:11:22:33:44:06	E-DIO24-334406");
    brdlst_append(plst, "192.168.1.107	00:11:22:33:44:07	E-DIO24-334407");
    brdlst_append(plst, "192.168.1.108	00:11:22:33:44:08	E-DIO24-334408");
    brdlst_append(plst, "192.168.1.109	00:11:22:33:44:09	E-DIO24-334409");
    brdlst_append(plst, "192.168.1.110	00:11:22:33:44:10	E-DIO24-334410");
    brdlst_append(plst, "192.168.1.111	00:11:22:33:44:11	E-DIO24-334411");
    brdlst_append(plst, "192.168.1.112	00:11:22:33:44:12	E-DIO24-334412");
    brdlst_append(plst, "192.168.1.113	00:11:22:33:44:13	E-DIO24-334413");
    brdlst_append(plst, "192.168.1.114	00:11:22:33:44:14	E-DIO24-334414");
    brdlst_append(plst, "192.168.1.115	00:11:22:33:44:15	E-DIO24-334415");
    brdlst_append(plst, "192.168.1.116	00:11:22:33:44:16	E-DIO24-334416");

    SECTION("test output atten's valid p arg") {
        for (i = 0; i < ARRAY_NUM(iodata_atten); i += 3) {
            CIUT_LOG ("output atten idx=%d", i/3);
            args.arg_cluster = iodata_atten[i + 0];
            args.arg_cmd = iodata_atten[i + 1];
            REQUIRE(0 == cuit_check_output(cb_cuit_check_output_doatten, &args, iodata_atten[i + 2]));
        }
    }
    SECTION("test output power's valid p arg") {
        for (i = 0; i < ARRAY_NUM(iodata_power); i += 3) {
            CIUT_LOG ("output power idx=%d", i/3);
            args.arg_cluster = iodata_power[i + 0];
            args.arg_cmd = iodata_power[i + 1];
            REQUIRE(0 == cuit_check_output(cb_cuit_check_output_dosetpower, &args, iodata_power[i + 2]));
        }
    }
    SECTION("test output power_read valid p arg") {
        for (i = 0; i < ARRAY_NUM(iodata_power_read); i += 3) {
            CIUT_LOG ("output power idx=%d", i/3);
            args.arg_cluster = iodata_power_read[i + 0];
            args.arg_cmd = iodata_power_read[i + 1];
            REQUIRE(0 == cuit_check_output(cb_cuit_check_output_dogetpower, &args, iodata_power_read[i + 2]));
        }
    }

    brdlst_clean(plst);
    REQUIRE(0 == plst->sz_max);
    REQUIRE(0 == plst->sz_cur);
    REQUIRE(NULL == plst->list);
    brdlst_init(plst);
    REQUIRE(0 == plst->sz_max);
    REQUIRE(0 == plst->sz_cur);
    REQUIRE(NULL == plst->list);
}

#endif /* CIUT_ENABLED */


/*****************************************************************************/
#if ! defined(CIUT_ENABLED) || (CIUT_ENABLED == 0)
static void
version (void)
{
    printf ("generate CCT control sequence.\n");
}

static void
help(const char * progname)
{
    assert (NULL != progname);

    fprintf (stderr, "Usage: \n"
            "\t%s [-h] [-v] [-r] [-p <POWER VALUES>] [-t <ATTEN VALUES>]\n"
            , progname);
    fprintf (stderr, "\nOptions:\n");
    fprintf (stderr, "\t-n <ATTEN VALUES>\tgenerate EDIO24 commands for attenuation\n");
    fprintf (stderr, "\t-w <POWER VALUES>\tgenerate EDIO24 commands for power on/off\n");
    fprintf (stderr, "\t-d \tgenerate EDIO24 commands for reading power status\n");

    fprintf (stderr, "\t-i <edio24 file>\tEDIO24 config file\n");
    fprintf (stderr, "\t-j <board file>\tCCT board config file\n");
    fprintf (stderr, "\t-a <ATTEN VALUES>\tgenerate EDIO24 commands for attenuation of a specify device\n");
    fprintf (stderr, "\t-p <POWER VALUES>\tgenerate EDIO24 commands for power on/off of a specify device\n");

    fprintf (stderr, "\t-h\tPrint this message.\n");
    fprintf (stderr, "\t-v\tVerbose information.\n");

    fprintf (stderr, "\nPOWER VALUES\n");
    fprintf (stderr, "\tvalues range from 0 to 7.\n");
    fprintf (stderr, "\t'p=0,1,2' -- pin 0, 1, and 2 will be ON.\n");
    fprintf (stderr, "\t'p=all' -- all ON.\n");
    fprintf (stderr, "\t'p=' -- all off.\n");

    fprintf (stderr, "\nATTEN VALUES\n");
    fprintf (stderr, "\t'p=0,1,2;v=31' -- pin 0, 1, and 2; value 31.\n");

    fprintf (stderr, "\nExamples for single EDIO24\n");
    fprintf (stderr, "\t%s -n 'p=0,1;v=21'\n", progname);
    fprintf (stderr, "\t%s -w 'p=all' \n", progname);
    fprintf (stderr, "\t%s -d 'p=0' \n", progname);

    fprintf (stderr, "\nExamples for cluster\n");
    fprintf (stderr, "\t* %s -i cct-edio24-list.txt -j cct-board-list.txt -c 'p=1' -a 'p=0,1;v=21'\n", progname);
    fprintf (stderr, "\t\tcluster 1, upper(0) and lower(1) attenuators are set to (combined) value 21.\n\n");
    fprintf (stderr, "\t* %s -i cct-edio24-list.txt -j cct-board-list.txt -c 'p=1' -p 'p=all' \n", progname);
    fprintf (stderr, "\t\tpower on all of boards in the cluster 1.\n\n");
    fprintf (stderr, "\t* %s -i cct-edio24-list.txt -j cct-board-list.txt -c 'p=all' -a 'p=0,1;v=62'\n", progname);
    fprintf (stderr, "\t\tall of clusters, upper(0) and lower(1) attenuators are set to (combined) value 62.\n\n");
    fprintf (stderr, "\t* %s -i cct-edio24-list.txt -j cct-board-list.txt -c 'p=all' -p 'p=all' \n", progname);
    fprintf (stderr, "\t\tpower on all of boards in all clusters.\n\n");
    fprintf (stderr, "\t* %s -i cct-edio24-list.txt -j cct-board-list.txt -c 'p=0' -b 'p=0'\n", progname);
    fprintf (stderr, "\t\tget the record line of the root('CCT_C00-00').\n\n");
    fprintf (stderr, "\t* %s -i cct-edio24-list.txt -j cct-board-list.txt -c 'p=16' -b 'p=8'\n", progname);
    fprintf (stderr, "\t\tget the record line of the board('CCT_C16-08').\n\n");
    fprintf (stderr, "\t* %s -i cct-edio24-list.txt -j cct-board-list.txt -c 'p=1' -r 'p=1' \n", progname);
    fprintf (stderr, "\t\tread the power status of the board at cluster 1('CCT_C01-01').\n\n");
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
    char * cluster = "";
    int c;
    struct option longopts[]  = {
        { "atten",        1, 0, 'n' }, /* the value of attenuation */
        { "power",        1, 0, 'w' }, /* the power pin values */
        { "powerread",    0, 0, 'd' }, /* get the power read command */

        { "config-edio24", 1, 0, 'i' }, /* the config file for EDIO24 */
        { "config-board", 1, 0, 'j' }, /* the config file for boards */
        { "getboard",     1, 0, 'b' }, /* get the device line, 0 for root, 1-8 for boards */
        { "setatten",     1, 0, 'a' }, /* set the attenuation. p: 0-upper, 1-lower */
        { "setpower",     1, 0, 'p' }, /* set the power on/off 0-8, 0 for root, 1-8 for boards */
        { "getpower",     1, 0, 'r' }, /* get the power status of 0-8, 0 for root, 1-8 for boards */
        { "cluster",      1, 0, 'c' }, /* the cluster number 0-16, 0 for root, 1-16 for boards */

        { "help",         0, 0, 'h' },
        { "verbose",      0, 0, 'v' },
        { 0,              0, 0,  0  },
    };

    while ((c = getopt_long( argc, argv, "w:n:dr:c:i:j:b:a:p:hv", longopts, NULL )) != EOF) {
        switch (c) {
            /* generate commands for single EDIO24 */
            case 'w':
                if (strlen (optarg) > 0) {
                    output_power(stdout, optarg);
                }
                break;
            case 'd':
                output_power_read(stdout, "");
                break;
            case 'n':
                if (strlen (optarg) > 0) {
                    output_atten(stdout, optarg);
                }
                break;

            /* generate commands for cluster */
            case 'c':
                cluster = optarg;
                break;
            case 'b':
                do_get_all_board(stdout, cluster, optarg);
                break;
            case 'i':
                read_file_edio24(optarg);
                break;
            case 'j':
                read_file_board(optarg);
                break;
            case 'a':
                do_setatten(stdout, cluster, optarg);
                break;
            case 'p':
                do_setpower(stdout, cluster, optarg);
                break;
            case 'r':
                do_getpower(stdout, cluster, optarg);
                break;

            case 'h':
                usage (argv[0]);
                //exit (0);
                break;
            case 'v':
                flg_verbose = 1;
                break;
            default:
                fprintf (stderr, "Unknown parameter: '%c'.\n", c);
                fprintf (stderr, "Use '%s -h' for more information.\n", argv[0]);
                //exit (-1);
                break;
        }
    }

    brdlst_clean(&g_lst_brd);
    brdlst_clean(&g_lst_edio24);
    return 0;
}

#endif /* CIUT_ENABLED */
