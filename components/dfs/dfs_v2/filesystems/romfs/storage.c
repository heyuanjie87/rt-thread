#include <rtthread.h>

#include <dfs_mnt.h>

#include "dfs_romfs.h"

int romfs_dev_read(struct dfs_mnt *mnt, unsigned long pos,
		   void *buf, size_t buflen)
{
    rt_device_read(mnt->dev_id, pos, buf, 1);

    return -EIO;
}
