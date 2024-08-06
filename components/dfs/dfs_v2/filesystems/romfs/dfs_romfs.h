/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019/01/13     Bernard      code cleanup
 */

#ifndef __DFS_ROMFS_H__
#define __DFS_ROMFS_H__

#include <rtthread.h>

#include <dfs_mnt.h>

#define ROMFS_DIRENT_FILE   0x00
#define ROMFS_DIRENT_DIR    0x01

struct romfs_dirent
{
    rt_uint32_t      type;  /* dirent type */

    const char       *name; /* dirent name */
    const rt_uint8_t *data; /* file date ptr */
    rt_size_t        size;  /* file size */
	rt_size_t        pos;
};

int dfs_romfs_init(void);
extern const struct romfs_dirent romfs_root;

struct romfs_mnt {
	struct dfs_vnode *root;
    rt_uint32_t maxsize;
};

typedef rt_uint32_t __be32;

rt_inline size_t romfs_maxsize(struct dfs_mnt *mnt)
{
	struct romfs_mnt *rmnt = mnt->data;

	return rmnt->maxsize;
}

#define ROMFS_MAGIC 0x7275

#define ROMFS_MAXFN 128

#define ROMSB_WORD0 "-rom"
#define ROMSB_WORD1 "1fs-"

/* On-disk "super block" */
struct romfs_super_block {
	char word0[4];
	char word1[4];
	__be32 size;
	__be32 checksum;
	char name[];		/* volume name */
};

/* On disk inode */
struct romfs_inode {
	__be32 next;		/* low 4 bits see ROMFH_ */
	__be32 spec;
	__be32 size;
	__be32 checksum;
	char name[];
};

#define ROMFH_TYPE 7
#define ROMFH_HRD 0
#define ROMFH_DIR 1
#define ROMFH_REG 2
#define ROMFH_SYM 3

/* Alignment */
#define ROMFH_SIZE 16
#define ROMFH_PAD (ROMFH_SIZE-1)
#define ROMFH_MASK (~ROMFH_PAD)

int romfs_dev_read(struct dfs_mnt *mnt, unsigned long pos,
		   void *buf, size_t buflen);
int romfs_dev_strnlen(struct dfs_mnt *mnt,
                      unsigned long pos, size_t maxlen);

#endif
