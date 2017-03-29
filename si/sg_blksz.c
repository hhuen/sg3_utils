/*
   Copyright (C) 2015 Samsung Semiconductor
   Hingkwan Huen (kwan.huen@ssi.samsung.com)

   Simple tool to download FW for Samsung SAS SSD

   Invocation: sg_fwdl -d <fw_dir_path> <scsi_device>

   Important note:
   Because device will need to reset to ROM mode, the Linux SCSI driver
   may remap device into different block device node (e.g., from /dev/sda
   to /dev/sdb) after the reset. If this happens you'll see device
   failing TUR after device comes back on after the reset. The solution
   for this is to use device link by path under the device tree. You'll
   can check this by:

   $ ls -l /dev/disk/by-path
   total 0
   lrwxrwxrwx 1 root root 9 Dec 18 16:07 pci-0000:03:00.0-sas-phy2-lun-0 -> ../../sdb
   lrwxrwxrwx 1 root root 9 Dec 18 12:41 pci-0000:03:00.0-sas-phy6-lun-0 -> ../../sda

   and

   $ lsscsi
   [0:0:0:0]    cd/dvd  MATSHITA DVD-ROM UJ8E0B   1.01  /dev/sr0
   [4:0:0:0]    disk    NETAPP   NETAPP UNIQUE ID NT06  /dev/sda
   [4:0:13:0]   disk    NETAPP   NETAPP UNIQUE ID NT08  /dev/sdb
   [5:0:0:0]    disk    ATA      Samsung SSD 850  2B6Q  /dev/sdc
   [6:0:0:0]    disk    ATA      INTEL SSDSC2BF48 TG20  /dev/sdd

   Then you can choose the device link from the by-path directory.
   E.g., if you are trying to update firmware for /dev/sdb, which is an "NETAPP" SSD version NT08:

   $ sudo ./sg_fwdl -d ./PM1633_FW_DIRECTORY /dev/disk/by-path/pci-0000\:03\:00.0-sas-phy2-lun-0

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

#define INQ_REPLY_LEN 128
#define INQ_CMD_LEN 6
#define TUR_CMD_LEN 6

#define EBUFF_SZ 256

#define SCSI_CMD_TIMEOUT 5000

int check_hdr_status(sg_io_hdr_t *p_io_hdr, const char* p_cmd)
{
    int ok = 0;
    char buff[64];
    switch (sg_err_category3(p_io_hdr)) {
    case SG_LIB_CAT_CLEAN:
        ok = 1;
        break;
    case SG_LIB_CAT_RECOVERED:
        printf("Recovered error from %s, continuing\n", p_cmd);
        ok = 1;
        break;
    default: /* won't bother decoding other categories */
	sprintf(buff, "%s command error", p_cmd);
        sg_chk_n_print3(buff, p_io_hdr, 1);
        break;
    }
    return ok;
}


int send_inquiry_cmd(int sg_fd) {
    int ok = 0;
    unsigned char sense_buffer[32];
    sg_io_hdr_t io_hdr;

    unsigned char inqBuff[INQ_REPLY_LEN];

    unsigned char inqCmdBlk [INQ_CMD_LEN] =
                                {0x12, 0, 0, 0, INQ_REPLY_LEN, 0};

    /* Prepare INQUIRY command */
    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(inqCmdBlk);
    /* io_hdr.iovec_count = 0; */  /* memset takes care of this */
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = INQ_REPLY_LEN;
    io_hdr.dxferp = inqBuff;
    io_hdr.cmdp = inqCmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = SCSI_CMD_TIMEOUT;     /* 20000 millisecs == 20 seconds */
    /* io_hdr.flags = 0; */     /* take defaults: indirect IO, etc */
    /* io_hdr.pack_id = 0; */
    /* io_hdr.usr_ptr = NULL; */

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_fwdl: Inquiry SG_IO ioctl error");
        return -1;
    }

    /* now for the error processing */
    ok = check_hdr_status(&io_hdr, "INQUIRY");
    if (ok) { /* output result if it is available */
        char * p = (char *)inqBuff;
        int f = (int)*(p + 7);
        printf("Device: %.8s  %.16s  %.4s  ", p + 8, p + 16, p + 32);
        printf("[wide=%d sync=%d cmdque=%d sftre=%d]\n",
               !!(f & 0x20), !!(f & 0x10), !!(f & 2), !!(f & 1));
        printf("Firmware Version: %.24s\n", p+96);

        /*printf("INQUIRY duration=%u millisecs, resid=%d, msg_status=%d\n",
                   io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status); */
    }
    return ok;
}


int mode_sense_cmd(int sg_fd, unsigned char* buff, unsigned int sz) {
    int ok = 0;
    unsigned char sense_buffer[32];
    sg_io_hdr_t io_hdr;
    unsigned short blk_sz;
    unsigned char *p = (unsigned char *) &blk_sz;

    unsigned char modCmdBlk [6] =
                                {0x1a, 0, 0x8a, 0, 0xff, 0};

    /* Prepare MODE_SENSE_6 command */
    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(modCmdBlk);
    /* io_hdr.iovec_count = 0; */  /* memset takes care of this */
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = sz;
    io_hdr.dxferp = buff;
    io_hdr.cmdp = modCmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = SCSI_CMD_TIMEOUT;     /* 20000 millisecs == 20 seconds */
    /* io_hdr.flags = 0; */     /* take defaults: indirect IO, etc */
    /* io_hdr.pack_id = 0; */
    /* io_hdr.usr_ptr = NULL; */

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_fwdl: Mode Sense SG_IO ioctl error");
        return -1;
    }

    /* now for the error processing */
    ok = check_hdr_status(&io_hdr, "MODE_SENSE");
    if (ok) {  /* output result if it is available */
	p[1] = buff[10];
	p[0] = buff[11];
        printf("Block Size from Block Descriptor : %u\n", blk_sz);
        printf("Mode Sense successful!\n");
    }
    else
        printf("Mode Sense failed!\n");
/*
    {
	unsigned int i;
	printf("Mode Page = ");
	for (i=0; i<sz; i++) {
		if (! (i%4))
			printf("\n%.2x: ", i);
		printf("%.2x ",buff[i]);
	}
	printf("\n");
    }
*/
        printf("MODE_SENSE duration=%u millisecs, resid=%d, msg_status=%d\n",
                   io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status);

    return ok;
}

int mode_select_cmd(int sg_fd, unsigned char* buff, unsigned int sz) {
    int ok = 0;
    unsigned char sense_buffer[32];
    sg_io_hdr_t io_hdr;

    unsigned char modCmdBlk [6] =
                                {0x15, 0x11, 0, 0, 0x18, 0};
/*
    unsigned int i;
    printf("Mode Page = ");
    for (i=0; i<sz; i++) {
	if (! (i%4))
		printf("\n%.2x: ", i);
		printf("%.2x ",buff[i]);
    }
    printf("\n");
*/
    /* Prepare MODE_SELECT_6 command */
    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(modCmdBlk);
    /* io_hdr.iovec_count = 0; */  /* memset takes care of this */
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    io_hdr.dxfer_len = sz;
    io_hdr.dxferp = buff;
    io_hdr.cmdp = modCmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = SCSI_CMD_TIMEOUT;     /* 20000 millisecs == 20 seconds */
    /* io_hdr.flags = 0; */     /* take defaults: indirect IO, etc */
    /* io_hdr.pack_id = 0; */
    /* io_hdr.usr_ptr = NULL; */

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_fwdl: Mode Select SG_IO ioctl error");
        return -1;
    }

    /* now for the error processing */
    ok = check_hdr_status(&io_hdr, "MODE_SELECT");
    if (ok)  /* output result if it is available */
        printf("Mode Select successful!\n");
    else
        printf("Mode Select failed!\n");
        /*printf("MODE_SELECT duration=%u millisecs, resid=%d, msg_status=%d\n",
                   io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status); */
    return ok;
}

int format_unit_cmd(int sg_fd) {
    int ok = 0;
    unsigned char sense_buffer[32];
    sg_io_hdr_t io_hdr;

    unsigned char fuCmdBlk [6] = {0x4, 0x10, 0, 0, 0, 0};
    unsigned char buff [4] = {0, 1, 0, 0};

    /* Prepare FORMAT_UNIT_6 command */
    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(fuCmdBlk);
    /* io_hdr.iovec_count = 0; */  /* memset takes care of this */
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    io_hdr.dxfer_len = 4;
    io_hdr.dxferp = buff;
    io_hdr.cmdp = fuCmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = SCSI_CMD_TIMEOUT;     /* 20000 millisecs == 20 seconds */
    /* io_hdr.flags = 0; */     /* take defaults: indirect IO, etc */
    /* io_hdr.pack_id = 0; */
    /* io_hdr.usr_ptr = NULL; */

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_fwdl: Format Unit SG_IO ioctl error");
        return -1;
    }

    /* now for the error processing */
    ok = check_hdr_status(&io_hdr, "FORMAT_UNIT");
    if (ok)  /* output result if it is available */
        printf("Format Unit successful!\n");
    else
        printf("Format Unit failed!\n");

        /* printf("FORMAT_UNIT duration=%u millisecs, resid=%d, msg_status=%d\n",
                   io_hdr.duration, io_hdr.resid, (int)io_hdr.msg_status); */
    return ok;
}

int change_block_size(int sg_fd, unsigned int sz) {
    int ok = 0;
    unsigned char buff[24];
    unsigned char *p=(unsigned char *) &sz;

    ok = mode_sense_cmd(sg_fd, buff, 24);
    if (ok == -1)
	return ok;
    buff[10] = p[1];
    buff[11] = p[0];
    ok = mode_select_cmd(sg_fd, buff, 24);
    if (ok == -1)
	return ok;
    ok = format_unit_cmd(sg_fd);
    if (ok == -1)
	return ok;
    ok = mode_sense_cmd(sg_fd, buff, 24);
    if (ok == -1)
	return ok;
    return ok;
}

int send_tur_cmd(int sg_fd) {
    int ok = 0;
    unsigned char sense_buffer[32];
    sg_io_hdr_t io_hdr;

    unsigned char turCmdBlk [TUR_CMD_LEN] =
                                {0x00, 0, 0, 0, 0, 0};

    /* Prepare TEST UNIT READY command */
    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(turCmdBlk);
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_NONE;
    io_hdr.cmdp = turCmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = SCSI_CMD_TIMEOUT;     /* 20000 millisecs == 20 seconds */

    if (ioctl(sg_fd, SG_IO, &io_hdr) < 0) {
        perror("sg_fwdl: Test Unit Ready SG_IO ioctl error");
        return -1;
    }

    /* now for the error processing */
    ok = check_hdr_status(&io_hdr, "TEST_UNIT_READY");
    if (ok)
        printf("Test Unit Ready successful so unit is ready!\n");
    else
        printf("Test Unit Ready failed so unit may _not_ be ready!\n");

    /*printf("TEST UNIT READY duration=%u millisecs, resid=%d, "
               "msg_status=%d\n", io_hdr.duration, io_hdr.resid,
               (int)io_hdr.msg_status); */
    return ok;
}


int main(int argc, char * argv[])
{
    int sg_fd;
    int k, ok;
    char ebuff[EBUFF_SZ];
    char * file_name = 0;
    unsigned short block_sz=512;

    for (k = 1; k < argc; ++k) {
        if (0 == memcmp("-s", argv[k], 2)) {
	    k++;
	    block_sz = atoi(argv[k]); /* expecting next argument to be block_sz */
            printf("New block size : %u\n", block_sz);
	}
        else if (*argv[k] == '-') {
            printf("Unrecognized switch: %s\n", argv[k]);
            file_name = 0;
            break;
        }
        else if (0 == file_name) {
            file_name = argv[k];
            printf("Device name: %s\n", file_name);
	}
        else {
            printf("too many arguments\n");
            file_name = 0;
            break;
        }
    }
    if (0 == file_name) {
        printf("Usage: 'sg_blksz -s <new_size> <sg_device>'\n");
        return 1;
    }

    /* N.B. An access mode of O_RDWR is required for some SCSI commands */
    if ((sg_fd = open(file_name, O_RDONLY)) < 0) {
        snprintf(ebuff, EBUFF_SZ,
                 "sg_fwdl: error device file: %s", file_name);
        perror(ebuff);
        return 1;
    }

    /* Just to be safe, check we have a new sg device by trying an ioctl */
    if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
        printf("sg_fwdl: %s doesn't seem to be an new sg device\n",
               file_name);
	goto error_exit;
    }

    ok = send_tur_cmd(sg_fd);
    if (ok == -1) {
	goto error_exit;
    }

    ok = send_inquiry_cmd(sg_fd);
    if (ok == -1) {
	goto error_exit;
    }

    ok = send_tur_cmd(sg_fd);
    if (ok == -1) {
	goto error_exit;
    }

    ok = change_block_size(sg_fd, block_sz);
    if (ok == -1) {
	goto error_exit;
    }

    close(sg_fd);
    return 0;

error_exit:
    close(sg_fd);
    return 1;

}
