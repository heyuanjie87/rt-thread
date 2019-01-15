/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Change Logs:
 * Date           Author       Notes
 * 2005-02-22     Bernard      The first version.
 * 2011-12-08     Bernard      Merges rename patch from iamcacy.
 * 2015-05-27     Bernard      Fix the fd clear issue.
 */

#include <dfs.h>
#include <dfs_file.h>
#include <dfs_private.h>

/**
 * @addtogroup FileApi
 */

/*@{*/

/**
 * this function will open a file which specified by path with specified flags.
 *
 * @param fd the file descriptor pointer to return the corresponding result.
 * @param path the specified file path.
 * @param flags the flags for open operator.
 *
 * @return 0 on successful, -1 on failed.
 */
int dfs_file_open(struct dfs_fd *fd, const char *path, int flags)
{
    struct dfs_filesystem *fs;
    int result;

    /* parameter check */
    if (fd == NULL)
        return -EINVAL;

    /* find filesystem */
    fs = dfs_filesystem_lookup(path);
    if (fs == NULL)
    {
        return -ENOENT;
    }

    LOG_D("open in filesystem:%s", fs->ops->name);
    fd->fs    = fs;             /* set file system */
    fd->fops  = fs->ops->fops;  /* set file ops */

    /* initialize the fd item */
    fd->type  = FT_REGULAR;
    fd->flags = flags;
    fd->size  = 0;
    fd->pos   = 0;
    fd->data  = fs;
    fd->path = (char*)path;

    /* specific file system open routine */
    if (fd->fops->open == NULL)
    {
        return -ENOSYS;
    }

    if ((result = fd->fops->open(fd)) < 0)
    {
        LOG_E("open failed");

        return result;
    }

    fd->flags |= DFS_F_OPEN;
    if (flags & O_DIRECTORY)
    {
        fd->type = FT_DIRECTORY;
        fd->flags |= DFS_F_DIRECTORY;
    }

    return 0;
}

/**
 * this function will close a file descriptor.
 *
 * @param fd the file descriptor to be closed.
 *
 * @return 0 on successful, -1 on failed.
 */
int dfs_file_close(struct dfs_fd *fd)
{
    int result = 0;

    if (fd == NULL)
        return -ENXIO;

    if (fd->fops->close != NULL)
        result = fd->fops->close(fd);

    /* close fd error, return */
    if (result < 0)
        return result;

    return result;
}

/**
 * this function will perform a io control on a file descriptor.
 *
 * @param fd the file descriptor.
 * @param cmd the command to send to file descriptor.
 * @param args the argument to send to file descriptor.
 *
 * @return 0 on successful, -1 on failed.
 */
int dfs_file_ioctl(struct dfs_fd *fd, int cmd, void *args)
{
    if (fd == NULL)
        return -EINVAL;

    /* regular file system fd */
    if (fd->type == FT_REGULAR)
    {
        switch (cmd)
        {
        case F_GETFL:
            return fd->flags; /* return flags */
        case F_SETFL:
            {
                int flags = (int)(rt_base_t)args;
                int mask  = O_NONBLOCK | O_APPEND;

                flags &= mask;
                fd->flags &= ~mask;
                fd->flags |= flags;
            }
            return 0;
        }
    }

    if (fd->fops->ioctl != NULL)
        return fd->fops->ioctl(fd, cmd, args);

    return -ENOSYS;
}

/**
 * this function will read specified length data from a file descriptor to a
 * buffer.
 *
 * @param fd the file descriptor.
 * @param buf the buffer to save the read data.
 * @param len the length of data buffer to be read.
 *
 * @return the actual read data bytes or 0 on end of file or failed.
 */
int dfs_file_read(struct dfs_fd *fd, void *buf, size_t len)
{
    int result = 0;

    if (fd == NULL)
        return -EINVAL;

    if (fd->fops->read == NULL)
        return -ENOSYS;

    if ((result = fd->fops->read(fd, buf, len)) < 0)
        fd->flags |= DFS_F_EOF;

    return result;
}

/**
 * this function will fetch directory entries from a directory descriptor.
 *
 * @param fd the directory descriptor.
 * @param dirp the dirent buffer to save result.
 * @param nbytes the available room in the buffer.
 *
 * @return the read dirent, others on failed.
 */
int dfs_file_getdents(struct dfs_fd *fd, struct dirent *dirp, size_t nbytes)
{
    /* parameter check */
    if (fd == NULL || fd->type != FT_DIRECTORY)
        return -EINVAL;

    if (fd->fops->getdents != NULL)
        return fd->fops->getdents(fd, dirp, nbytes);

    return -ENOSYS;
}

/**
 * this function will unlink (remove) a specified path file from file system.
 *
 * @param path the specified path file to be unlinked.
 *
 * @return 0 on successful, -1 on failed.
 */
int dfs_file_unlink(const char *path)
{
    int result;
    struct dfs_filesystem *fs;

    /* get filesystem */
    if ((fs = dfs_filesystem_lookup(path)) == NULL)
    {
        result = -ENOENT;
        goto __exit;
    }

    if (fs->ops->unlink != NULL)
    {
        result = fs->ops->unlink(fs, path);
    }
    else result = -ENOSYS;

__exit:
    return result;
}

/**
 * this function will write some specified length data to file system.
 *
 * @param fd the file descriptor.
 * @param buf the data buffer to be written.
 * @param len the data buffer length
 *
 * @return the actual written data length.
 */
int dfs_file_write(struct dfs_fd *fd, const void *buf, size_t len)
{
    if (fd == NULL)
        return -EINVAL;

    if (fd->fops->write == NULL)
        return -ENOSYS;

    return fd->fops->write(fd, buf, len);
}

/**
 * this function will flush buffer on a file descriptor.
 *
 * @param fd the file descriptor.
 *
 * @return 0 on successful, -1 on failed.
 */
int dfs_file_flush(struct dfs_fd *fd)
{
    if (fd == NULL)
        return -EINVAL;

    if (fd->fops->flush == NULL)
        return -ENOSYS;

    return fd->fops->flush(fd);
}

/**
 * this function will seek the offset for specified file descriptor.
 *
 * @param fd the file descriptor.
 * @param offset the offset to be sought.
 *
 * @return the current position after seek.
 */
int dfs_file_lseek(struct dfs_fd *fd, off_t offset, int whence)
{
    int result;

    if (fd == NULL)
        return -EINVAL;

    if (fd->fops->lseek == NULL)
        return -ENOSYS;

    result = fd->fops->lseek(fd, offset, whence);

    return result;
}

/**
 * this function will get file information.
 *
 * @param path the file path.
 * @param buf the data buffer to save stat description.
 *
 * @return 0 on successful, -1 on failed.
 */
int dfs_file_stat(const char *path, struct stat *buf)
{
    int result = -ENOSYS;
    struct dfs_filesystem *fs;

    if ((fs = dfs_filesystem_lookup(path)) == NULL)
    {
        LOG_E("can't find mounted filesystem on path:%s", path);
        return -ENOENT;
    }

    if (fs->ops->stat)
    {
        result = fs->ops->stat(fs, path, buf);
    }

    return result;
}

/**
 * this function will rename an old path name to a new path name.
 *
 * @param oldpath the old path name.
 * @param newpath the new path name.
 *
 * @return 0 on successful, -1 on failed.
 */
int dfs_file_rename(const char *oldpath, const char *newpath)
{
    int result = -ENOSYS;
    struct dfs_filesystem *oldfs, *newfs;

    oldfs = dfs_filesystem_lookup(oldpath);
    newfs = dfs_filesystem_lookup(newpath);

    if (oldfs && (oldfs == newfs))
    {
        if (oldfs->ops->rename)
        {
            result = oldfs->ops->rename(oldfs, oldpath, newpath);
        }
    }
    else
    {
        result = -EXDEV;
    }

    return result;
}

/* @} */
