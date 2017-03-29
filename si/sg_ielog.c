/*
   This program performs a LOG_SENSE_10 command to retrier
   Informational Exceptions page  [0x2f]

*  Copyright (C) Samsung Semiconductor
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.

   Invocation: sg_ielog <scsi_device>
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

#include <time.h>

void waitFor (long secs) {
    unsigned int retTime;
    if (secs <= 0)
        return;
    retTime = time(0) + secs;     // Get finishing time.
    while (time(0) < retTime);    // Loop until it arrives.
}

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

int process_err(sg_io_hdr_t *hdr)
{
    int ok = 0;
    switch (sg_err_category3(hdr)) {
    case SG_LIB_CAT_CLEAN:
        ok = 1;
        break;
    case SG_LIB_CAT_UNIT_ATTENTION:
        printf("Unit attention error on LOG_SENSE_10, continuing anyway\n");
        ok = 1;
        break;
    case SG_LIB_CAT_RECOVERED:
        printf("Recovered error on LOG_SENSE_10, continuing\n");
        ok = 1;
        break;
    default: /* won't bother decoding other categories */
        sg_chk_n_print3("LOG_SENSE_10 command error", hdr, 1);
        break;
    }
    return ok;
}

int main(int argc, char * argv[])
{
    int sg_fd, k, ok, num, next;
    /* initial 4-byte read from IE log page code 0x2f */
    unsigned char rc10CmdBlk [10] =
                { 0x4d, 0x00, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00 };

    sg_io_hdr_t io_hdr;
    char * file_name = 0;
    unsigned char rcBuff[512];
    unsigned char sense_buffer[64];
    unsigned short alloc_len = 4;
    unsigned long long host_sect=0, nand_sect=0;
    unsigned short pc;
    unsigned char  plen;
    unsigned char *ucp;
    int do_verb = 0;
    long delay = 0;

    for (k = 1; k < argc; ++k) {
	if (0 == memcmp("-d", argv[k], 2)) {
		delay = atol(argv[k+1]);
	   printf("delay: %lu\n", delay);
		k++;
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
        printf("Usage: 'sg_ielog -d <delay_in_sec> <sg_device>'\n");
        return 1;
    }

    if ((sg_fd = open(file_name, O_RDWR)) < 0) {
        printf("sg_ielog: error opening file: %s", file_name);
        return 1;
    }
    /* Just to be safe, check we have a new sg device by trying an ioctl */
    if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
        printf("sg_ielog: %s doesn't seem to be an new sg device\n", file_name);
        close(sg_fd);
        return 1;
    }

    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(rc10CmdBlk);
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = sizeof(rcBuff);
    io_hdr.dxferp = rcBuff;
    io_hdr.cmdp = rc10CmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = 20000;     /* 20000 millisecs == 20 seconds */

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_ielog: LOG_SENSE_10 SG_IO ioctl error");
        close(sg_fd);
        return 1;
    }

    ok = process_err(&io_hdr);
    if (ok) { /* output result if it is available */
       if (do_verb) {
          printf("LOG_SENSE_10 duration=%u millisecs, resid=%d, msg_status=%d\n",
               io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status);
          printf("   Received Buff: ");
          for (k=0; k<alloc_len; k++)
            printf(" %.2x", rcBuff[k]);
          printf("\n");
       }
       /* Update LOG_SENSE_10 request byte count */
       rc10CmdBlk[7] = rcBuff[2];
       rc10CmdBlk[8] = rcBuff[3] + 4;
       alloc_len = (rcBuff[3]+4) | (rcBuff[2] << 8);
   while (1) {
       time_t ltime; /* calendar time */
       ltime=time(NULL); /* get current cal time */
       printf("%s",asctime( localtime(&ltime) ) );

       if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
           perror("sg_ielog: LOG_SENSE_10 SG_IO ioctl error");
           close(sg_fd);
           return 1;
       }

       /* now for the error processing */
       ok = process_err(&io_hdr);
       if (ok) { /* output result if it is available */
          if (do_verb) {
             printf("LOG_SENSE_10 duration=%u millisecs, resid=%d, msg_status=%d\n",
               io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status);
             printf("   Received Buff: ");
             for (k=0; k<alloc_len; k++) {
		if (!(k % 0x10))
			printf("\n%.3x	", k);
			printf(" %.2x", rcBuff[k]);
	     }
             printf("\n");
          }
          ucp = &rcBuff[0] + 4;
          num = alloc_len - 4;
          if (num < 4) {
              perror("sg_ielog: badly formed Informational Exceptions page");
              close(sg_fd);
              return 1;
          }
          next = 0;
          for (k = num; k > 0; k -= next, ucp += next) {
	     if (k < 3) {
                 perror("sg_ielog: short Informational Exceptions page\n");
                 close(sg_fd);
                 return 1;
	     }
	     next = ucp[3] + 4; /* total len of the current param */
	     pc = (ucp[0] << 8) + ucp[1];
	     if (pc == 0xe9) {
		plen = ucp[3];
		if (plen == 8)
			host_sect = endian_swap_64(*((unsigned long long *) &ucp[4]));
                printf("   found 0xe9 param: data_units_written = %llu \n", host_sect);
	     }
	     else if (pc == 0xeb) {
		plen = ucp[3];
		if (plen == 8)
			nand_sect = endian_swap_64(*((unsigned long long *) &ucp[4]));
                printf("   found 0xeb param: nand_data_units_written = %llu \n", nand_sect);
	     }
          }
       }
       if (delay)
		waitFor (delay);
       else
	break;
   }
    }

    close(sg_fd);
    return 0;
}
