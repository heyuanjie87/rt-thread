#include <rtthread.h>
#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <sys/stat.h>
#include "xipfs.h"

static void xipfs_dev_init(struct xipfs_dev *dev, rt_nor_t *nor)
{

}

static struct xipfs_dev *xipfs_dev_get(struct dfs_filesystem *fs)
{
    rt_nor_t *nor;

    nor = (rt_nor_t*)fs->dev_id;
    return (struct xipfs_dev *)nor->parent.priv;
}

static int dfs_xipfs_mount(struct dfs_filesystem *fs, unsigned long rwflag, const void *data)
{
    struct xipfs_dev *dev;
    rt_nor_t *nor;
    int ret;

    nor = (rt_nor_t*)fs->dev_id;
    if (nor->parent.parent.type != RT_Device_Class_MTD)
        return -ENODEV;
    if (nor->parent.type != MTD_TYPE_NOR)
        return -ENODEV;
    dev = rt_malloc(sizeof(struct xipfs_dev));
    if (!dev)
        return -ENOMEM;

    xipfs_dev_init(dev, nor);
    ret = xipfs_mount(dev)
    if (ret == 0)
        nor->parent.priv = dev;

    return ret;
}

static int dfs_xipfs_unmount(struct dfs_filesystem *fs)
{
    rt_nor_t *nor;

    nor = (rt_nor_t*)fs->dev_id;
    rt_free(nor->parent.priv);
    nor->parent.priv = 0;

    return 0;
}

static int dfs_xipfs_mkfs(rt_device_t did)
{
    struct xipfs_dev *dev;

    dev = (struct xipfs_dev*)did->user_data;
    xipfs_mkfs(dev);

    return 0;
}

static int dfs_xipfs_open(struct dfs_fd *file)
{
    int ret;
    struct xipfs_dev *dev;
    struct xipfs_dirent *ent;

    dev = xipfs_dev_get(file->fs);

    ret = xipfs_open(dev, file->path, file->flags, &ent);
    if (ret >= 0)
    {
        file->size = ret;
        file->data = ent;
        ret = 0;
    }

    return ret;
}

static int dfs_xipfs_close(struct dfs_fd *file)
{
    struct xipfs_dev *dev;

    dev = xipfs_dev_get(file->fs);

    xipfs_close(dev, (struct xipfs_dirent *)file->data, file->pos);
    file->data = NULL;

    return 0;
}

static int dfs_xipfs_write(struct dfs_fd *fd, const void *buf, size_t count)
{
    int ret;
    struct xipfs_dev *dev;

    dev = xipfs_dev_get(fd->fs);
    ret = xipfs_write(dev, (struct xipfs_dirent *)fd->data, fd->pos, (uint8_t *)buf, count);
    if (ret > 0)
        fd->pos += ret;

    return ret;
}

static int dfs_xipfs_getdents(struct dfs_fd *file, struct dirent *dirp, uint32_t count)
{
    struct xipfs_dev *dev;
    int index;
    const char *name;
    struct dirent *d;
    struct xipfs_dirent *subrd;

    dev = xipfs_dev_get(file->fs);
    /* make integer count */
    count = (count / sizeof(struct dirent));
    if (count == 0)
        return 0;

    for (index = 0; index < count && file->pos < file->size; index ++)
    {
        d = dirp + index;

        if (xipfs_getdirent(dev, file->data, file->pos, &subrd) == 0)
            break;

        name = subrd->name;
        /* fill dirent */
        if (subrd->type == XIPFS_DTYPE_DIR)
            d->d_type = DT_DIR;
        else
            d->d_type = DT_REG;

        d->d_namlen = rt_strlen(name);
        d->d_reclen = (rt_uint16_t)sizeof(struct dirent);
        rt_strncpy(d->d_name, name, d->d_namlen + 1);

        /* move to next position */
        file->pos += sizeof(*subrd);
    }

    return index * sizeof(struct dirent);
}

static int dfs_xipfs_read(struct dfs_fd *file, void *buf, size_t count)
{
    struct xipfs_dev *dev;
    int ret;

    dev = xipfs_dev_get(file->fs);
    ret = xipfs_read(dev, (struct xipfs_dirent *)file->data, file->pos, buf, count);
    if (ret > 0)
        file->pos += ret;

    return ret;
}

static int dfs_xipfs_stat(struct dfs_filesystem *fs, const char *path, struct stat *st)
{
    struct xipfs_dev *dev;

    dev = xipfs_dev_get(fs);

    return xipfs_stat(dev, path, st);
}

static int dfs_xipfs_ioctl(struct dfs_fd *file, int cmd, void *args)
{
    int ret = -EIO;
    struct xipfs_dev *dev;

    dev = xipfs_dev_get(file->fs);
#if 0
    switch (cmd)
    {
    case FIOMMAP:
    {
        ret = xipfs_mmap(dev, (struct xipfs_dirent*)file->data, args);
    }
    break;
    default:
        break;
    }
#endif
    return ret;
}

static int dfs_xipfs_unlink(struct dfs_filesystem *fs, const char *pathname)
{
    struct xipfs_dev *dev;

    dev = xipfs_dev_get(fs);

    return xipfs_unlink(dev, pathname);
}

static int dfs_xipfs_lseek(struct dfs_fd *fd, off_t offset)
{
    struct xipfs_dirent *d;

    d = (struct xipfs_dirent *)fd->data;
    if (fd->flags != O_RDONLY || offset > d->size)
        return -EIO;

    fd->pos = offset;

    return offset;
}

static const struct dfs_file_ops _xipfs_fops =
{
    dfs_xipfs_open,
    dfs_xipfs_close,
    dfs_xipfs_ioctl,
    dfs_xipfs_read,
    dfs_xipfs_write,
    NULL,
    dfs_xipfs_lseek,
    dfs_xipfs_getdents,
	NULL,
};

static const struct dfs_filesystem_ops _xipfs =
{
    "xipfs",
    DFS_FS_FLAG_DEFAULT,
    &_xipfs_fops,

    dfs_xipfs_mount,
    dfs_xipfs_unmount,
    dfs_xipfs_mkfs,
    NULL,

    dfs_xipfs_unlink,
    dfs_xipfs_stat,
    NULL,
};

int dfs_xipfs_init(void)
{
    /* register rom file system */
    dfs_register(&_xipfs);
    return 0;
}
INIT_COMPONENT_EXPORT(dfs_xipfs_init);
