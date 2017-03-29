/*
   This program performs a BO_CONTROL command

*  Copyright (C) Samsung Semiconductor
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.

   Invocation: sg_boctrl <-o n> <scsi_device>
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
                { 0x9e, 0x15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    sg_io_hdr_t io_hdr;
    char * file_name = 0;
    unsigned char sense_buffer[32];
    unsigned short bo_ctrl;
    unsigned char  bo_time;

    for (k = 1; k < argc; ++k) {
        if (0 == strcmp("-o", argv[k])) {
	    ++k;
	    bo_ctrl = atoi(argv[k]); /* expecting next argument to be bo_ctrl */
	    rc16CmdBlk[2] = (bo_ctrl << 6) & 0xff;
	}
        else if (0 == strcmp("-t", argv[k])) {
	    ++k;
	    bo_time = atoi(argv[k]); /* expecting next argument to be bo_time */
	    rc16CmdBlk[3] = bo_time / 100; /* BO_CTRL time in units of 100ms */
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
        printf("Usage: 'sg_boctrl <-o opcode>  <sg_device>'\n");
        printf("Example - No Change : 'sg_boctrl -o 0 <sg_device>'\n");
        printf("Example - Start Op  : 'sg_boctrl -o 1 <sg_device>'\n");
        printf("Example - Stop Op   : 'sg_boctrl -o 2 <sg_device>'\n");
        return 1;
    }

    if ((sg_fd = open(file_name, O_RDWR)) < 0) {
        printf("sg_boctrl: error opening file: %s", file_name);
        return 1;
    }
    /* Just to be safe, check we have a new sg device by trying an ioctl */
    if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
        printf("sg_boctrl: %s doesn't seem to be an new sg device\n", file_name);
        close(sg_fd);
        return 1;
    }

    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    memset(sense_buffer, 0, 32);
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(rc16CmdBlk);
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    io_hdr.cmdp = rc16CmdBlk;
    io_hdr.sbp = sense_buffer;

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_ststat: BO_CONTROL SG_IO ioctl error");
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
        printf("Unit attention error on BO_CONTROL, continuing anyway\n");
        ok = 1;
        break;
    case SG_LIB_CAT_RECOVERED:
        printf("Recovered error on BO_CONTROL, continuing\n");
        ok = 1;
        break;
    default: /* won't bother decoding other categories */
        sg_chk_n_print3("BO_CONTROL command error", &io_hdr, 1);
        break;
    }

    if (ok) { /* output result if it is available */
       printf("BO_CONTROL duration=%u millisecs, resid=%d, msg_status=%d\n",
               io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status);
    }

    close(sg_fd);
    return 0;
}
