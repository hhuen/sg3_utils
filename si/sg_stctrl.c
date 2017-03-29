/*
   This program performs a STREAM_CONTROL command for
	STR_CTL: 0x1 (Open Stream); 0x2 (Close Stream)

*  Copyright (C) Samsung Semiconductor
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.

   Invocation: sg_stctrl <-o|-i n> <scsi_device>
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
    int sg_fd, k, ok;
    unsigned char rc16CmdBlk [16] =
                { 0x9e, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    sg_io_hdr_t io_hdr;
    char * file_name = 0;
    unsigned char rcBuff[8];
    unsigned char sense_buffer[32];
    unsigned char op_type=0;
    unsigned short str_id;

    for (k = 1; k < argc; ++k) {
        if (0 == strcmp("-o", argv[k])) {
            op_type = 1; /* open a stream */
	    rc16CmdBlk[1] |= (op_type<<5);
	    rc16CmdBlk[13] = 8; /* expecting 8 bytes in return buff */
	}
        else if (0 == strcmp("-c", argv[k])) {
            op_type = 2; /* close a stream */
	    ++k;
	    str_id = atoi(argv[k]); /* expecting next argument to be str_id */
	    if (str_id>32) {
		printf("Invalid Stream ID Entered: %s\n", argv[k]);
		file_name = 0;
                op_type = 0;
		break;
	    }
	    rc16CmdBlk[1] |= (op_type<<5);
	    rc16CmdBlk[4] = (str_id >> 8) & 0xff;
	    rc16CmdBlk[5] = str_id & 0xff;
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
    if (0 == file_name || 0==op_type) {
        printf("Usage: 'sg_stctrl <-o|-c id>  <sg_device>'\n");
        printf("Example - Open Stream: 'sg_stctrl -o <sg_device>'\n");
        printf("Example - Close Stream: 'sg_stctrl -c <str_id_num> <sg_device>'\n");
        return 1;
    }

    if ((sg_fd = open(file_name, O_RDWR)) < 0) {
        printf("sg_stctrl: error opening file: %s", file_name);
        return 1;
    }
    /* Just to be safe, check we have a new sg device by trying an ioctl */
    if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
        printf("sg_stctrl: %s doesn't seem to be an new sg device\n", file_name);
        close(sg_fd);
        return 1;
    }

    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    memset(rcBuff, 0, 8);
    memset(sense_buffer, 0, 32);
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(rc16CmdBlk);
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    if (op_type == 1) { /* open stream */
       io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
       io_hdr.dxfer_len = sizeof(rcBuff);
       io_hdr.dxferp = rcBuff;
    }
    else if (op_type == 2) { /* close stream */
       io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
       io_hdr.dxfer_len = 0;
       io_hdr.dxferp = 0;
    }
    io_hdr.cmdp = rc16CmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = 20000;     /* 20000 millisecs == 20 seconds */

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_stctrl: STREAM CONTROL SG_IO ioctl error");
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
        printf("Unit attention error on STREAM_CONTROL, continuing anyway\n");
        ok = 1;
        break;
    case SG_LIB_CAT_RECOVERED:
        printf("Recovered error on STREAM_CONTROL, continuing\n");
        ok = 1;
        break;
    default: /* won't bother decoding other categories */
        sg_chk_n_print3("STREAM_CONTROL command error", &io_hdr, 1);
        break;
    }

    if (ok) { /* output result if it is available */
       printf("STREAM_CONTROL duration=%u millisecs, resid=%d, msg_status=%d\n",
               io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status);
       if (op_type == 1) { /* open stream */
          printf("   Received Buff: ");
          for (k=0; k<8; k++)
               printf(" %.2x", rcBuff[k]);
          printf("\n");
          printf("STREAM_CONTROL open stream with id %u returned\n", rcBuff[4]<<8 | rcBuff[5]);
       }

    }

    close(sg_fd);
    return 0;
}
