/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-09-30     Bernard      first version.
 * 2017-11-08     JasonJiaJie  fix memory leak issue when close a pipe.
 */

#include <rtthread.h>
#include <rthw.h>
#include <dfs_file.h>
#include <dfs_poll.h>
#include "pipe.h"

#define pipe_lock(pipe) rt_mutex_take(&((pipe)->lock), -1);
#define pipe_unlock(pipe) rt_mutex_release(&((pipe)->lock));

static int pipe_fops_open(struct dfs_fd *file)
{
    rt_pipe_t *pipe;
    const char *name;
    struct dfs_filesystem *fs;

    fs = file->data;
    name = file->path + rt_strlen(fs->path);
    if (file->flags & O_CREAT)
    {
        pipe = rt_pipe_find(name);
        if (pipe)
            return -EEXIST;
        pipe = rt_pipe_create(name, PIPE_BUFSZ);
        if (!pipe)
            return -ENOMEM;
        file->fops = fs->ops->fops;
        file->data = pipe;

        return 0;
    }

    pipe = rt_pipe_find(name);
    if (!pipe)
        return -ENODEV;
    file->fops = fs->ops->fops;

    pipe_lock(pipe);

    switch (file->flags & O_ACCMODE)
    {
    case O_RDONLY:
        pipe->readers ++;
        break;
    case O_WRONLY:
        pipe->writers ++;
        break;
    case O_RDWR:
        pipe->readers ++;
        pipe->writers ++;
        break;
    }
    pipe->ref_count ++;

    pipe_unlock(pipe);
    file->data = pipe;

    return 0;
}

static int pipe_fops_close(struct dfs_fd *fd)
{
    rt_pipe_t *pipe;

    pipe = (rt_pipe_t *)fd->data;
    if (pipe->ref_count == 0)
        return 0;

    pipe_lock(pipe);

    switch (fd->flags & O_ACCMODE)
    {
    case O_RDONLY:
        pipe->readers --;
        break;
    case O_WRONLY:
        pipe->writers --;
        break;
    case O_RDWR:
        pipe->readers --;
        pipe->writers --;
        break;
    }

    if (pipe->writers == 0)
    {
        rt_wqueue_wakeup(&(pipe->reader_queue), (void*)(POLLIN | POLLERR | POLLHUP));
    }

    if (pipe->readers == 0)
    {
        rt_wqueue_wakeup(&(pipe->writer_queue), (void*)(POLLOUT | POLLERR | POLLHUP));
    }

    if (pipe->ref_count == 1)
        rt_pipe_remove(pipe);
    pipe->ref_count --;

    pipe_unlock(pipe);

    if (pipe->ref_count == 0)
        rt_pipe_delete(pipe);

    return 0;
}

static int pipe_fops_ioctl(struct dfs_fd *fd, int cmd, void *args)
{
    rt_pipe_t *pipe;
    int ret = 0;

    pipe = (rt_pipe_t *)fd->data;

    switch (cmd)
    {
    case FIONREAD:
        *((int*)args) = rt_ringbuffer_data_len(pipe->fifo);
        break;
    case FIONWRITE:
        *((int*)args) = rt_ringbuffer_space_len(pipe->fifo);
        break;
    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

static int pipe_fops_read(struct dfs_fd *fd, void *buf, size_t count)
{
    int len = 0;
    rt_pipe_t *pipe;

    pipe = (rt_pipe_t *)fd->data;

    /* no process has the pipe open for writing, return end-of-file */
    if (pipe->writers == 0)
        return 0;

    pipe_lock(pipe);

    while (1)
    {
        if (pipe->writers == 0)
        {
            goto out;
        }

        len = rt_ringbuffer_get(pipe->fifo, buf, count);

        if (len > 0)
        {
            break;
        }
        else
        {
            if (fd->flags & O_NONBLOCK)
            {
                len = -EAGAIN;
                goto out;
            }

            rt_wqueue_wakeup(&(pipe->writer_queue), (void*)POLLOUT);
            rt_wqueue_wait(&(pipe->reader_queue), 0, -1);
        }
    }

    /* wakeup writer */
    rt_wqueue_wakeup(&(pipe->writer_queue), (void*)POLLOUT);

out:
    pipe_unlock(pipe);

    return len;
}

static int pipe_fops_write(struct dfs_fd *fd, const void *buf, size_t count)
{
    int len;
    rt_pipe_t *pipe;
    int wakeup = 0;
    int ret = 0;
    uint8_t *pbuf;

    pipe = (rt_pipe_t *)fd->data;

    if (pipe->readers == 0)
    {
        ret = -EPIPE;
        goto out;
    }

    if (count == 0)
        return 0;

    pbuf = (uint8_t*)buf;
    pipe_lock(pipe);

    while (1)
    {
        if (pipe->readers == 0)
        {
            if (ret == 0)
                ret = -EPIPE;
            break;
        }

        len = rt_ringbuffer_put(pipe->fifo, pbuf, count - ret);
        ret +=  len;
        pbuf += len;
        wakeup = 1;

        if (ret == count)
        {
            break;
        }
        else
        {
            if (fd->flags & O_NONBLOCK)
            {
                if (ret == 0)
                {
                    ret = -EAGAIN;
                }

                break;
            }
        }

        pipe_unlock(pipe);
        rt_wqueue_wakeup(&(pipe->reader_queue), (void*)POLLIN);
        /* pipe full, waiting on suspended write list */
        rt_wqueue_wait(&(pipe->writer_queue), 0, -1);
        pipe_lock(pipe);
    }
    pipe_unlock(pipe);

    if (wakeup)
    {
        rt_wqueue_wakeup(&(pipe->reader_queue), (void*)POLLIN);
    }

out:
    return ret;
}

static int pipe_fops_poll(struct dfs_fd *fd, rt_pollreq_t *req)
{
    int mask = 0;
    rt_pipe_t *pipe;
    int mode = 0;
    pipe = (rt_pipe_t *)fd->data;

    rt_poll_add(&(pipe->reader_queue), req);
    rt_poll_add(&(pipe->writer_queue), req);

    switch (fd->flags & O_ACCMODE)
    {
    case O_RDONLY:
        mode = 1;
        break;
    case O_WRONLY:
        mode = 2;
        break;
    case O_RDWR:
        mode = 3;
        break;
    }

    if (mode & 1)
    {
        if (rt_ringbuffer_data_len(pipe->fifo) != 0)
        {
            mask |= POLLIN;
        }
        if (pipe->writers == 0)
        {
            mask |= POLLHUP;
        }
    }

    if (mode & 2)
    {
        if (rt_ringbuffer_space_len(pipe->fifo) != 0)
        {
            mask |= POLLOUT;
        }
        if (pipe->readers == 0)
        {
            mask |= POLLERR;
        }
    }

    return mask;
}

static const struct dfs_file_ops pipe_fops =
{
    pipe_fops_open,
    pipe_fops_close,
    pipe_fops_ioctl,
    pipe_fops_read,
    pipe_fops_write,
    NULL,
    NULL,
    NULL,
    pipe_fops_poll,
};

static int dfs_pipefs_mount(struct dfs_filesystem* fs, unsigned long rwflag, const void* data)
{
    return 0;
}

static int dfs_pipefs_unlink(struct dfs_filesystem *fs, const char *path)
{
    rt_pipe_t *pipe;
    const char *name;
    //todo lock
    name = path + rt_strlen(fs->path);
    pipe = rt_pipe_find(name);
    if (!pipe)
        return -ENODEV;
    if (pipe->ref_count)
        return -EBUSY;
    rt_pipe_remove(pipe);
    rt_pipe_delete(pipe);

    return 0;
}

static const struct dfs_filesystem_ops _pipe_fs =
{
    "pipefs",
    DFS_FS_FLAG_DEFAULT,
    &pipe_fops,

    dfs_pipefs_mount,
    NULL,
    NULL, /* mkfs */
    NULL, /* statfs */

    dfs_pipefs_unlink, /* unlink */
    NULL, /* stat */
    NULL, /* rename */
};

int dfs_skt_init(void)
{
    int ret;

    ret = dfs_register(&_pipe_fs);
    if (ret == 0)
        ret = dfs_pseudo_mount(":pipe", &_pipe_fs, 0);

    return ret;
}
INIT_COMPONENT_EXPORT(dfs_skt_init);
