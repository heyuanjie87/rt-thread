#include <rtthread.h>

#include <dfs_mnt.h>

#include "dfs_romfs.h"

int romfs_dev_read(struct dfs_mnt *mnt, unsigned long pos,
		   void *buf, size_t buflen)
{
    rt_off_t sector = pos / 512;
    rt_ssize_t ret = -EIO;
    char *alignbuf;
    rt_size_t cnt;

    if (((pos % 512) == 0) && ((buflen % 512) == 0))
    {
        cnt = buflen/512;
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
            size_t blen = buflen;
            unsigned long pos_in_sector;
            char *cbuf = buf;
            int cplen;

            while (blen)
            {
                cnt = rt_device_read(mnt->dev_id, sector, alignbuf, 1);
                if (cnt != 1)
                    break;

                pos_in_sector = pos % 512;
                cplen = 512 - pos_in_sector;

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
