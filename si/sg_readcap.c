/*
   This program performs a READ_CAP_16 command as scsi mid-level support
   16 byte commands

*  Copyright (C) Samsung Semiconductor
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.

   Invocation: sg_readcap <scsi_device>
*/

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "sg_lib.h"
#include "sg_io_linux.h"

inline unsigned short  endian_swap_16(unsigned short x)
{
    return (unsigned short) ((x>>8) | (x<<8));
}

inline unsigned int endian_swap_32(unsigned int x)
{
    return (unsigned int) ((x>>24) |
        ((x<<8) & 0x00FF0000) |
        ((x>>8) & 0x0000FF00) |
        (x<<24));
}

// __int64 for MSVC, "long long" for gcc
inline unsigned long long  endian_swap_64(unsigned long long x)
{
    return (unsigned long long) ( (x>>56) |
        ((x<<40) & 0x00FF000000000000) |
        ((x<<24) & 0x0000FF0000000000) |
        ((x<<8)  & 0x000000FF00000000) |
        ((x>>8)  & 0x00000000FF000000) |
        ((x>>24) & 0x0000000000FF0000) |
        ((x>>40) & 0x000000000000FF00) |
        (x<<56));
}

int main(int argc, char * argv[])
{
    int sg_fd, k, ok;
    unsigned char rc16CmdBlk [16] =
                { 0x9e, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0, 0 };

    sg_io_hdr_t io_hdr;
    char * file_name = 0;
    unsigned char rcBuff[64];
    unsigned char sense_buffer[64];
    unsigned long long  num_sect;
    unsigned int sect_sz;
    int prot_en, p_type;

    for (k = 1; k < argc; ++k) {
        if (*argv[k] == '-') {
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
        printf("Usage: 'sg_readcap <sg_device>'\n");
        return 1;
    }

    if ((sg_fd = open(file_name, O_RDWR)) < 0) {
        printf("sg_readcap: error opening file: %s", file_name);
        return 1;
    }
    /* Just to be safe, check we have a new sg device by trying an ioctl */
    if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
        printf("sg_readcap: %s doesn't seem to be an new sg device\n", file_name);
        close(sg_fd);
        return 1;
    }

    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(rc16CmdBlk);
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = sizeof(rcBuff);
    io_hdr.dxferp = rcBuff;
    io_hdr.cmdp = rc16CmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = 20000;     /* 20000 millisecs == 20 seconds */

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_readcap: READ_CAP_16 SG_IO ioctl error");
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
        printf("Unit attention error on READ_CAP_16, continuing anyway\n");
        ok = 1;
        break;
    case SG_LIB_CAT_RECOVERED:
        printf("Recovered error on READ_CAP_16, continuing\n");
        ok = 1;
        break;
    default: /* won't bother decoding other categories */
        sg_chk_n_print3("READ_CAP_16 command error", &io_hdr, 1);
        break;
    }

    if (ok) { /* output result if it is available */
       printf("READ_CAP_16 duration=%u millisecs, resid=%d, msg_status=%d\n",
               io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status);
       printf("   Received Buff: ");
       for (k=0; k<32; k++)
            printf(" %.2x", rcBuff[k]);
       printf("\n");
       num_sect = endian_swap_64(*((unsigned long long *) rcBuff)) + 1;
       sect_sz =  endian_swap_32(*((unsigned int *) (rcBuff+8)));

       prot_en = !!(rcBuff[12] & 0x1);
       p_type = ((rcBuff[12] >> 1) & 0x7);

       printf("   Protection: prot_en=%d, p_type=%d, p_i_exponent=%d",
                   prot_en, p_type, ((rcBuff[13] >> 4) & 0xf));
       if (prot_en)
          printf(" [type %d protection]\n", p_type + 1);
       else
          printf("\n");

       printf("number of sectors=%llu, sector size=%u\n",
            num_sect, sect_sz);

    }

    close(sg_fd);
    return 0;
}
