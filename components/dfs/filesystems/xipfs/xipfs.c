/*
 * File          : xipfs.c
   Features  : (a filesysterm based on norflash)
               1 read/write surpport,only one writer can be exist
               2 dirents fix to XIPFS_DIRENT_NUM,in block 0
               3 only support root and empty directories
               4 file has size limit(XIPFS_MASK_SIZE*32)
               5 linear storage of file data
               6 file can be deleted,its space will be free when need
               7 only support append write,exist file can not be writed

 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date        Author          Notes
   20180124    heyuanjie87     first version
 */

#include <rtthread.h>
#include "xipfs.h"

#define XIPFS_MAGIC                0x58495046
#define XIPFS_MASK_SIZE       (1024*8)
#define XIPFS_ENT_END          0xEDED

static uint32_t mask_getsize(uint32_t sm)
{
    uint32_t size = 0;

    sm = ~sm;

    while (sm)
    {
        size ++;
        sm &= (sm-1);
    }

    return size * XIPFS_MASK_SIZE;
}

static uint32_t mask_setsize(uint32_t s)
{
    int n;
    uint32_t m = 0;

    n = (s - 1)/XIPFS_MASK_SIZE;
    do
    {
        m |= (1<<n);
        n --;
    }
    while(n >= 0);

    return m;
}

static struct xipfs_dirent *xipfs_drent_lookup(struct xipfs_dirent *root, uint32_t start, const char *path, int *size)
{
    int index;
    const char *subpath, *subpath_end;
    struct xipfs_dirent *dirent;
    int dirent_size = 0;

    *size = 0;
    dirent = (struct xipfs_dirent *)(root->data + start);
    for (index = 0; index < XIPFS_DIRENT_NUM; index ++)
    {
        if (dirent->magic != XIPFS_MAGIC)
            break;
        if (dirent->valid)
            dirent_size += sizeof(struct xipfs_dirent);

        dirent ++;
    }

    if (path[0] == '/' && path[1] == '\0')
    {
        *size = dirent_size;
        root->size = dirent_size;
        return root;
    }

    /* get the end position of this subpath */
    subpath_end = path;
    /* skip /// */
    while (*subpath_end && *subpath_end == '/')
        subpath_end ++;
    subpath = subpath_end;
    while ((*subpath_end != '/') && *subpath_end)
        subpath_end ++;

    /* search in folder */
    dirent = (struct xipfs_dirent *)(root->data + start);
    for (index = 0; index < dirent_size; dirent ++)
    {
        if ((dirent->type != XIPFS_DTYPE_FILE) && dirent->type != XIPFS_DTYPE_DIR)
            return NULL;

        if (!dirent->valid)
            continue;

        if (rt_strlen(dirent->name) == (subpath_end - subpath) &&
                rt_strncmp(dirent->name, subpath, (subpath_end - subpath)) == 0)
        {
            if (dirent->size == 0xFFFFFFFF)
            {
                *size = mask_getsize(dirent->size);
            }
            else
            {
                *size = dirent->size;
            }

            return dirent;
        }

        index += sizeof(struct xipfs_dirent);
    }

    /* not found */
    return NULL;
}

static int xipfs_flash_write(struct xipfs_dev *dev, uint32_t addr, void *buf, uint32_t len)
{
    if (len == 0)
        return 0;

    if (addr > (dev->start + dev->ta_size) || (addr < dev->start))
        return -ENOMEM;

    if ((addr + len) > (dev->start + dev->ta_size))
        len = dev->start + dev->ta_size - addr;

    dev->ops->write(addr, buf, len);

    return len;
}

static int set_head(struct xipfs_dev *dev, uint32_t mvto)
{
    struct xipfs_head hdr;

    hdr.magic = XIPFS_MAGIC;
    hdr.mvto = mvto;

    return xipfs_flash_write(dev, dev->start, &hdr, sizeof(hdr));
}

static int erase_block(struct xipfs_dev *dev, int n)
{
    return dev->ops->erase(dev->start + (n * dev->blksize), dev->blksize);
}

static void backup_block(struct xipfs_dev *dev, int n)
{
    uint32_t dst, src;

    erase_block(dev, dev->nblk - 1);
    src = dev->start + n * dev->blksize;
    dst = dev->start + dev->blksize * (dev->nblk - 1);
    xipfs_flash_write(dev, dst, (void*)src, dev->blksize);
}

static void restore_dirent(struct xipfs_dev *dev)
{
    struct xipfs_dirent ent, *dsrc, *ddst;
    int i;
    uint32_t pos = sizeof(struct xipfs);

    ddst = (struct xipfs_dirent*)(dev->start + dev->root.data);
    dsrc = (struct xipfs_dirent*)((uint32_t)ddst + dev->av_size);
    for (i = 0; i < XIPFS_DIRENT_NUM; i ++, dsrc++)
    {
        if (dsrc->magic != XIPFS_MAGIC)
            break;
        /* skip deleted and bad files */
        if (!dsrc->valid || 
			dsrc->end != XIPFS_ENT_END ||
            dsrc->size == 0xFFFFFFFF)
            continue;
        /* skip zero file */
        if (dsrc->type == XIPFS_DTYPE_FILE && dsrc->size == 0)
			continue;

        ent = *dsrc;
        ent.data = pos;
        pos = pos + ((ent.size + 3)/4) * 4;

        /* set new dirent */
        xipfs_flash_write(dev, (uint32_t)ddst, &ent, sizeof(ent));
        /* backup old dirent */
        xipfs_flash_write(dev, (uint32_t)(ddst + XIPFS_DIRENT_NUM), dsrc, sizeof(*dsrc));
        ddst ++;
    }
}

static void free_block0(struct xipfs_dev *dev)
{
    backup_block(dev, 0);
    erase_block(dev, 0);
    set_head(dev, 0xFFFE);
    restore_dirent(dev);
}

static void do_move(struct xipfs_dev *dev, uint32_t dst, struct xipfs_dirent *bak, int *curblk)
{
    uint32_t src;
    uint32_t pos;
    uint32_t blksize;
    int b;
    int len, size;

    blksize = dev->blksize;
    pos = bak->data;
    size = bak->size;
    dst += dev->start;

    while (size > 0)
    {
        b = pos/blksize;
        if (b != *curblk)
        {
			/* walk to next block */
            *curblk = b;
            backup_block(dev, b);
            len = size > blksize? blksize : size;
        }
        else
        {
            len = (b + 1) * blksize - bak->data;
        }

        src = dev->start + dev->av_size + pos%blksize;

        xipfs_flash_write(dev, dst, (void*)src, len);
        size -= len;
        dst += len;
        pos += len;
    }
}

static void move_data(struct xipfs_dev *dev)
{
    struct xipfs_dirent *ent, *bak;
    int n;
    int curblk = 0;

    ent = (struct xipfs_dirent *)(dev->start + dev->root.data);
    for (n = 0; n < XIPFS_DIRENT_NUM; n ++)
    {
        if (ent->magic != XIPFS_MAGIC)
            break;

        bak = ent + XIPFS_DIRENT_NUM;
        if (ent->size == 0)
            continue;

        do_move(dev, ent->data, bak, &curblk);
    }
}

static void xipfs_free_blocks(struct xipfs_dev *dev)
{
    set_head(dev, dev->nblk - 1); /* mark data moving start */
    free_block0(dev);
    move_data(dev);
    set_head(dev, 0); /* mark data moving end */  
}

static struct xipfs_dirent * create_dirent(struct xipfs_dev *dev, char *path, uint32_t flag)
{
    struct xipfs_dirent *dirent = 0, *ds;
    int i = 0;
    uint32_t addr;
    int delfnd;

_again:
    addr = sizeof(struct xipfs);
    delfnd = 0;
    ds = (struct xipfs_dirent *)(dev->start + sizeof(struct xipfs_head));
    for (i = 0; i < XIPFS_DIRENT_NUM; i++, ds ++)
    {
        uint32_t size;

        if (ds->magic == 0xFFFFFFFF)
        {
            dirent = ds;
            break;
        }
        else if (ds->magic == XIPFS_MAGIC)
        {
            if (ds->type == XIPFS_DTYPE_DIR)
                continue;

            size = ds->size;
            if (size == 0xFFFFFFFF)
			{
                /* bad file treat as deleted */
                delfnd ++;
			    size = mask_getsize(ds->smask);
			}

            if (ds->end != XIPFS_ENT_END)
			{
				/* bad dirent treat as deleted */
				delfnd ++;
			    continue;
			}

            addr =  ds->data + size;
            if (!ds->valid)
                delfnd ++;
        }
    }
    /* have no available space */
    if (addr >= dev->av_size)
    {
        dirent = NULL ;
    }

    if ((dirent == NULL) && (delfnd == XIPFS_DIRENT_NUM))
    {
		/* erase all when all files are deleted */
        dev->ops->erase(dev->start, dev->av_size);
        dirent = (struct xipfs_dirent *)(dev->start + dev->root.data);
        addr = sizeof(struct xipfs);
    }
    else if (dirent == NULL)
	{
		/* have folders */
        xipfs_free_blocks(dev);
        goto _again;	
	}

    if (dirent)
    {
        struct xipfs_dirent nd;

        nd.magic = XIPFS_MAGIC;
        nd.end = XIPFS_ENT_END;
        nd.valid = 0xFF;
        nd.dless = 0xFF;

        if (flag & O_DIRECTORY)
        {
            nd.type = XIPFS_DTYPE_DIR;
            nd.size = 0;
            nd.data = 0xFFFFFFF0;
        }
        else
        {
            nd.type = XIPFS_DTYPE_FILE;
            nd.size = 0xFFFFFFFF;
            nd.data = ((addr+3)/4)*4;
        }

        nd.smask = 0xFFFFFFFF;
        rt_strncpy(nd.name, &path[1], rt_strlen(path));
        xipfs_flash_write(dev, (uint32_t)dirent, (void*)&nd, sizeof(nd));
        if (dirent->magic != XIPFS_MAGIC)
            dirent = NULL;
    }

    return dirent;
}

static void mark_dataless(struct xipfs_dev *dev, struct xipfs_dirent *d)
{
	uint8_t dless = 0;

    if (d->dless != 0xFF)
		return;
	xipfs_flash_write(dev, (uint32_t)&d->dless, &dless, 1);
}

int xipfs_open(struct xipfs_dev *dev, char *path, uint32_t flag, struct xipfs_dirent **ent)
{
    struct xipfs_dirent* dirent;
    int size;

    if (flag & (O_RDWR))
        return -EIO;

_again:
    dirent = xipfs_drent_lookup(&dev->root, dev->start, path, &size);
    if (dirent == NULL)
    {
        if (flag & O_RDONLY)
            return -ENOENT;

        if (flag & O_CREAT)
        {
            dev->ops->mutex(dev->userdata, 1);
            if (dev->writer != 0)
            {
                dev->ops->mutex(dev->userdata, 0);
                return -EBUSY;
            }
            dev->writer = (void*)1;
            dev->ops->mutex(dev->userdata, 0);

            dirent = create_dirent(dev, path, flag);

            dev->writer = dirent;
            if (dirent == 0)
                return -ENOMEM;
        }
        else
            return -EIO;
    }
    else
    {
        if (flag & O_TRUNC)
        {
            uint8_t valid = 0;

            xipfs_flash_write(dev, (uint32_t)&dirent->valid, &valid, 1);
            goto _again;
        }

        if (flag & O_CREAT)
            return -EEXIST;
    }

    /* entry is a directory file type */
    if (dirent->type == XIPFS_DTYPE_DIR)
    {
        if (!(flag & O_DIRECTORY))
            return -ENOENT;
    }
    else
    {
        /* entry is a file, but open it as a directory */
        if (flag & O_DIRECTORY)
            return -ENOENT;
    }
    *ent = dirent;

    return size;
}

int xipfs_close(struct xipfs_dev *dev, struct xipfs_dirent *d, uint32_t size)
{
    if (dev->writer == d)
    {
        xipfs_flash_write(dev, (uint32_t)&d->size, &size, sizeof(size));
        dev->writer = 0;
    }

    return 0;
}

int xipfs_mount(struct xipfs_dev *dev)
{
    struct xipfs *fs;

    if ((dev->nblk < 2) || (dev->blksize < sizeof(struct xipfs)))
        return -1;

    dev->ta_size = dev->blksize * dev->nblk;
    dev->av_size = dev->ta_size - dev->blksize;

    fs = (struct xipfs *)dev->start;
    if (fs->hdr.magic != XIPFS_MAGIC && fs->hdr.magic != 0xFFFFFFFF)
        return -1;

    if ((fs->hdr.mvto != 0xFFFFFFFF) && (fs->hdr.mvto != 0))
    {
        xipfs_mkfs(dev);
    }

    dev->root.data = sizeof(struct xipfs_head);
    dev->root.magic = XIPFS_MAGIC;
    dev->root.type = XIPFS_DTYPE_DIR;
    dev->root.name[0] = '/';
    dev->root.size = 0;

    return 0;
}

int xipfs_mkfs(struct xipfs_dev *dev)
{
    int ret = 0;
 
    dev->ops->erase(dev->start, (dev->blksize * dev->nblk - 1));

    return ret;
}

int xipfs_read(struct xipfs_dev *dev, struct xipfs_dirent *d, uint32_t pos, uint8_t *buf, uint32_t size)
{
    uint32_t src;

    if (d->size == 0xFFFFFFFF)
        return -EBADF;
    if (pos >= d->size)
        return 0;
    if ((pos + size) > d->size)
        size = d->size - pos;
    src = dev->start + d->data + pos;
    rt_memcpy(buf, (void*)src, size);

    return size;
}

int xipfs_write(struct xipfs_dev *dev, struct xipfs_dirent *d, uint32_t pos, uint8_t *buf, uint32_t size)
{
    uint32_t dst;
    uint32_t mask;

    dst = d->data + pos;
    if (dst >= dev->av_size)
	{
	    mark_dataless(dev, d);
		return -ENOMEM;
	}

    dst += size;
    if (dst >= dev->av_size)
	{
	    mark_dataless(dev, d);
		size = dev->av_size - (d->data + pos);
	}

    mask = mask_setsize(pos + size);
    if (d->smask & mask)
    {
        mask = ~mask;
        xipfs_flash_write(dev, (uint32_t)&d->smask, &mask, 4);
    }
    dst = dev->start + d->data + pos;

    return xipfs_flash_write(dev, dst, buf, size);
}

int xipfs_getdirent(struct xipfs_dev *dev, struct xipfs_dirent *root, uint32_t pos, struct xipfs_dirent **ent)
{
    struct xipfs_dirent *d;
    int ind, i, n = 0;

    if (pos >= (XIPFS_DIRENT_NUM * sizeof(struct xipfs_dirent)))
        return 0;

    ind = pos/sizeof(*d);
    d = (struct xipfs_dirent *)(root->data + dev->start);
    for (i = 0; i < XIPFS_DIRENT_NUM; i ++)
    {
        if (d->magic != XIPFS_MAGIC)
            break;

        if (d->valid)
        {
            if (n == ind)
            {
                *ent = d;
                return 1;
            }
            n ++;
        }
        d ++;
    }

    return 0;
}

int xipfs_stat(struct xipfs_dev *dev, const char *path, struct stat *st)
{
    int size;
    struct xipfs_dirent *dirent;

    dirent = xipfs_drent_lookup(&dev->root, dev->start, path, &size);

    if (dirent == NULL)
        return -ENOENT;

    if (dirent->size == 0xFFFFFFFF)
        return -EBADF;

    st->st_dev = 0;
    st->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH |
                  S_IWUSR | S_IWGRP | S_IWOTH;

    if (dirent->type == XIPFS_DTYPE_DIR)
    {
        st->st_mode &= ~S_IFREG;
        st->st_mode |= S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;
    }

    st->st_size = dirent->size;
    st->st_mtime = 0;

    return 0;
}

int xipfs_mmap(struct xipfs_dev *dev, struct xipfs_dirent *d, uint32_t *addr)
{
    if (d->type != XIPFS_DTYPE_FILE)
        return -EIO;

    if (d->size == 0xFFFFFFFF || d->dless != 0xFF)
        return -EBADF;
    *addr = d->data + dev->start;

    return 0;
}

int xipfs_unlink(struct xipfs_dev *dev, const char *path)
{
    int ret = -1;
    int size;
    struct xipfs_dirent *d;

    d = xipfs_drent_lookup(&dev->root, dev->start, path, &size);
    if (d && (d != &dev->root))
    {
        uint8_t valid = 0;

        xipfs_flash_write(dev, (uint32_t)&d->valid, &valid, 1);
        ret = 0;
    }

    return ret;
}
