#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include "sg_lib.h"
#include "sg_io_linux.h"

/* This program performs a READ_16 command as scsi mid-level support
   16 byte commands from lk 2.4.15

*  Copyright (C) 2001 D. Gilbert
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.

   Invocation: sg_rw_stream -i <id> <scsi_device>

   Version 1.02 (20020206)

*/

#define READ16_BUFF_LEN 16384
#define READ16_CMD_LEN 16

#define WRITE16_BUFF_LEN 16384
#define WRITE16_CMD_LEN 16

#define EBUFF_SZ 256

int main(int argc, char * argv[])
{
    int sg_fd, rok, wok;

    unsigned short str_id, k;

    unsigned char r16CmdBlk [READ16_CMD_LEN] =
                {0x88, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    unsigned char w16CmdBlk [WRITE16_CMD_LEN] =
                {0x9a, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    sg_io_hdr_t io_hdr;

    char * file_name = 0;
    char ebuff[EBUFF_SZ];

    unsigned char inBuff[READ16_BUFF_LEN];
    unsigned char outBuff[WRITE16_BUFF_LEN];

    unsigned char sense_buffer[32];

    srand(time(NULL));

    for (k = 1; k < argc; ++k) {
	if (0 == strcmp("-i", argv[k])) {
            ++k;
            str_id = atoi(argv[k]); /* expecting next argument to be strid */
            if (str_id>32) {
                printf("Invalid Stream ID Entered: %s\n", argv[k]);
                file_name = 0;
                break;
            }
            w16CmdBlk[10] = (str_id>>8) & 0xff;
            w16CmdBlk[11] = str_id & 0xff;
        }
        else if (*argv[k] == '-') {
            printf("Unrecognized switch: %s\n", argv[k]);
            file_name = 0;
            break;
        }
        else if (0 == file_name)
            file_name = argv[k];
        else {
            printf("too many arguments\n");
            file_name = 0;
            break;
        }
    }
    if (0 == file_name) {
        printf("Usage: 'sg_rw_stream -i <id> <sg_device>'\n");
        return 1;
    }

    if ((sg_fd = open(file_name, O_RDWR)) < 0) {
        snprintf(ebuff, EBUFF_SZ,
                 "sg_rw_stream: error opening file: %s", file_name);
        perror(ebuff);
        return 1;
    }
    /* Just to be safe, check we have a new sg device by trying an ioctl */
    if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
        printf("sg_rw_stream: %s doesn't seem to be a new sg device\n",
               file_name);
        close(sg_fd);
        return 1;
    }
    /* transfer length is number of contiguous data blocks */
    r16CmdBlk[12] = ((READ16_BUFF_LEN/512)>>8) & 0xff;
    r16CmdBlk[13] = (READ16_BUFF_LEN/512) & 0xff;
    w16CmdBlk[12] = ((WRITE16_BUFF_LEN/512)>>8) & 0xff;
    w16CmdBlk[13] = (WRITE16_BUFF_LEN/512) & 0xff;

    printf("sg_rw_stream: SG_GET_VERSION_NUM %d\n", k);

    for (k = 0; k < WRITE16_BUFF_LEN; k++)
	outBuff[k] = (unsigned char) rand();

    printf("WR CDB: ");
    for (k = 0; k < WRITE16_CMD_LEN; k++)
	printf("%.2x ", w16CmdBlk[k]);
    printf("\n");

    /* Prepare WRITE_16 command */
    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(w16CmdBlk);
    /* io_hdr.iovec_count = 0; */  /* memset takes care of this */
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    io_hdr.dxfer_len = WRITE16_BUFF_LEN;
    io_hdr.dxferp = outBuff;
    io_hdr.cmdp = w16CmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = 20000;     /* 20000 millisecs == 20 seconds */
    /* io_hdr.flags = 0; */     /* take defaults: indirect IO, etc */
    /* io_hdr.pack_id = 0; */
    /* io_hdr.usr_ptr = NULL; */

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_rw_stream: Inquiry SG_IO ioctl error");
        close(sg_fd);
        return 1;
    }

    /* now for the error processing */
    wok = 0;
    switch (sg_err_category3(&io_hdr)) {
    case SG_LIB_CAT_CLEAN:
        wok = 1;
        break;
    case SG_LIB_CAT_RECOVERED:
        printf("Recovered error on WRITE_16, continuing\n");
        wok = 1;
        break;
    default: /* won't bother decoding other categories */
        sg_chk_n_print3("WRITE_16 command error", &io_hdr, 1);
        break;
    }

    if (wok) { /* output result if it is available */
	printf("WRITE_16 duration=%u millisecs, resid=%d, msg_status=%d\n",
               io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status);
	printf("WRITE BUFFER: \n");
	for (k=0;k < 8; k++)
		printf("%.2x ", outBuff[k]);
	printf("...\n");
    }


    printf("RD CDB: ");
    for (k = 0; k < READ16_CMD_LEN; k++)
	printf("%.2x ", r16CmdBlk[k]);
    printf("\n");
    /* Prepare READ_16 command */
    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(r16CmdBlk);
    /* io_hdr.iovec_count = 0; */  /* memset takes care of this */
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = READ16_BUFF_LEN;
    io_hdr.dxferp = inBuff;
    io_hdr.cmdp = r16CmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = 20000;     /* 20000 millisecs == 20 seconds */
    /* io_hdr.flags = 0; */     /* take defaults: indirect IO, etc */
    /* io_hdr.pack_id = 0; */
    /* io_hdr.usr_ptr = NULL; */

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_rw_stream: Inquiry SG_IO ioctl error");
        close(sg_fd);
        return 1;
    }

    /* now for the error processing */
    rok = 0;
    switch (sg_err_category3(&io_hdr)) {
    case SG_LIB_CAT_CLEAN:
        rok = 1;
        break;
    case SG_LIB_CAT_RECOVERED:
        printf("Recovered error on READ_16, continuing\n");
        rok = 1;
        break;
    default: /* won't bother decoding other categories */
        sg_chk_n_print3("READ_16 command error", &io_hdr, 1);
        break;
    }

    if (rok) { /* output result if it is available */
        printf("READ_16 duration=%u millisecs, resid=%d, msg_status=%d\n",
               io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status);

	printf("READ BUFFER: \n");
	for (k=0;k < 8; k++)
		printf("%.2x ", inBuff[k]);
	printf("...\n");
    }

    if (rok && wok)
	for (k=0;k < WRITE16_BUFF_LEN; k++)
		if (outBuff[k] != inBuff[k])
			printf("COMPARE ERROR pos=%d, out_val=%u, in_val=%u\n",
				k, outBuff[k], inBuff[k]);

    close(sg_fd);
    return 0;
}
