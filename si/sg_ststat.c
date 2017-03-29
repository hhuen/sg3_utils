/*
   This program performs a STREAM_STATUS command

*  Copyright (C) Samsung Semiconductor
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.

   Invocation: sg_ststat <-i n> <scsi_device>
*/

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "sg_lib.h"
#include "sg_io_linux.h"

int main(int argc, char * argv[])
{
    int sg_fd, ok, k;
    unsigned char rc16CmdBlk [16] =
                { 0x9e, 0x16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    sg_io_hdr_t io_hdr;
    char * file_name = 0;
    unsigned char rcBuff[128+8]; /* 16 streams * 8-byte desc + 8-byte header */
    unsigned char sense_buffer[32];
    unsigned short str_id;  /* default to start with stream id 1 */
    unsigned int  alloc_len = 8;

    for (k = 1; k < argc; ++k) {
        if (0 == strcmp("-i", argv[k])) {
	    ++k;
	    str_id = atoi(argv[k]); /* expecting next argument to be str_id */
	    if (str_id<1 || str_id>16) {
		printf("Invalid Stream ID Entered: %s\n", argv[k]);
		file_name = 0;
		break;
	    }
	    alloc_len += (16-str_id+1)*8;
	    rc16CmdBlk[4] = (str_id >> 8) & 0xff;
	    rc16CmdBlk[5] = str_id & 0xff;
	    rc16CmdBlk[10] = (alloc_len >> 24) & 0xff;
	    rc16CmdBlk[11] = (alloc_len >> 16) & 0xff;
	    rc16CmdBlk[12] = (alloc_len >> 8 ) & 0xff;
	    rc16CmdBlk[13] = (alloc_len) & 0xff;
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
        printf("Usage: 'sg_ststat <-i id>  <sg_device>'\n");
        return 1;
    }

    if ((sg_fd = open(file_name, O_RDWR)) < 0) {
        printf("sg_ststat: error opening file: %s", file_name);
        return 1;
    }
    /* Just to be safe, check we have a new sg device by trying an ioctl */
    if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
        printf("sg_ststat: %s doesn't seem to be an new sg device\n", file_name);
        close(sg_fd);
        return 1;
    }

    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    memset(rcBuff, 0, 8);
    memset(sense_buffer, 0, 32);
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(rc16CmdBlk);
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = (alloc_len < sizeof(rcBuff)) ? alloc_len : sizeof(rcBuff);
    io_hdr.dxferp = rcBuff;
    io_hdr.cmdp = rc16CmdBlk;
    io_hdr.sbp = sense_buffer;

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_ststat: STREAM STATUS SG_IO ioctl error");
        close(sg_fd);
        return 1;
    }

    /* now for the error processing */
    ok = 0;
    switch (sg_err_category3(&io_hdr)) {
    case SG_LIB_CAT_CLEAN:
        ok = 1;
        break;
    case SG_LIB_CAT_UNIT_ATTENTION:
        printf("Unit attention error on STREAM_STATUS, continuing anyway\n");
        ok = 1;
        break;
    case SG_LIB_CAT_RECOVERED:
        printf("Recovered error on STREAM_STATUS, continuing\n");
        ok = 1;
        break;
    default: /* won't bother decoding other categories */
        sg_chk_n_print3("STREAM_STATUS command error", &io_hdr, 1);
        break;
    }

    if (ok) { /* output result if it is available */
       printf("STREAM_STATUS duration=%u millisecs, resid=%d, msg_status=%d\n",
               io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status);
       printf("   Number of open stream:  %u\n", rcBuff[6]<<8 | rcBuff[7]);
       printf("   Received Buff: ");
       for (k=0; k<(int) alloc_len; k++) {
	    if ((k%8) == 0)
		printf("\n");
	    printf(" %.2x", rcBuff[k]);
       }
       printf("\n");
    }

    close(sg_fd);
    return 0;
}
