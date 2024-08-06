#include <rtthread.h>

#include <dfs_mnt.h>

#include "dfs_romfs.h"

int romfs_dev_read(struct dfs_mnt *mnt, unsigned long pos,
                   void *buf, size_t buflen)
{
    rt_off_t   sector = pos / 512;
    rt_ssize_t ret    = -EIO;
    char      *alignbuf;
    rt_size_t  cnt;

    if (((pos % 512) == 0) && ((buflen % 512) == 0))
    {
        cnt = buflen / 512;
        ret = rt_device_read(mnt->dev_id, sector, buf, cnt);
        if (ret = cnt)
            ret = buflen;
    }
    else
    {
        alignbuf = rt_malloc(512);

        if (!alignbuf)
        {
            ret = -ENOMEM;
        }
        else
        {
            size_t        blen = buflen;
            unsigned long pos_in_sector;
            char         *cbuf = buf;
            int           cplen;

            while (blen)
            {
                cnt = rt_device_read(mnt->dev_id, sector, alignbuf, 1);
                if (cnt != 1)
                    break;

                pos_in_sector = pos % 512;
                cplen         = 512 - pos_in_sector;
                if (cplen > blen)
                    cplen = blen;

                rt_memcpy(cbuf, &alignbuf[pos_in_sector], cplen);
                cbuf += cplen;
                blen -= cplen;
            }

            rt_free(alignbuf);

            if (blen == 0)
                ret = buflen;
        }
    }

    return ret;
}

/*
 * determine the length of a string in romfs on a block device
 */
static ssize_t romfs_blk_strnlen(struct dfs_mnt *mnt,
                                 unsigned long pos, size_t limit)
{
    ssize_t n = 0;
    ssize_t segment;
    char    buf[32];
    int     rlen;

    /* scan the string up to blocksize bytes at a time */
    while (limit > 0)
    {
        rt_memset(buf, 0, sizeof(buf));

        rlen = sizeof(buf) - 1;
        if (rlen > limit)
            rlen = limit;

        segment = romfs_dev_read(mnt, pos, buf, rlen);
        if (segment != rlen)
            break;

        segment  = rt_strlen(buf);
        limit   -= segment;
        pos     += segment;
        n       += segment;

        if (segment != rlen)
            break;
    }

    return n;
}

int romfs_dev_strnlen(struct dfs_mnt *mnt,
                      unsigned long pos, size_t maxlen)
{
    size_t limit;

    limit = romfs_maxsize(mnt);
    if (pos >= limit)
        return -EIO;
    if (maxlen > limit - pos)
        maxlen = limit - pos;

    return romfs_blk_strnlen(mnt, pos, maxlen);
}
