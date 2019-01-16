/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2005-02-22     Bernard      The first version.
 * 2010-06-30     Bernard      Optimize for RT-Thread RTOS
 * 2011-03-12     Bernard      fix the filesystem lookup issue.
 * 2017-11-30     Bernard      fix the filesystem_operation_table issue.
 * 2017-12-05     Bernard      fix the fs type search issue in mkfs.
 */

#include <dfs_fs.h>
#include <dfs_file.h>
#include "dfs_private.h"

static rt_list_t _fs_list = {&_fs_list, &_fs_list};

/**
 * @addtogroup FsApi
 */
/*@{*/

/**
 * this function will register a file system instance to device file system.
 *
 * @param ops the file system instance to be registered.
 *
 * @return 0 on successful, -1 on failed.
 */
int dfs_register(const struct dfs_filesystem_ops *ops)
{
    int ret = 0;
    const struct dfs_filesystem_ops **empty = NULL;
    const struct dfs_filesystem_ops **iter;

    /* lock filesystem */
    dfs_lock();
    /* check if this filesystem was already registered */
    for (iter = &filesystem_operation_table[0];
           iter < &filesystem_operation_table[DFS_FILESYSTEM_TYPES_MAX]; iter ++)
    {
        /* find out an empty filesystem type entry */
        if (*iter == NULL)
            (empty == NULL) ? (empty = iter) : 0;
        else if (strcmp((*iter)->name, ops->name) == 0)
        {
            ret = -EEXIST;
            break;
        }
    }

    /* save the filesystem's operations */
    if (empty == NULL)
    {
        LOG_E("There is no space to register this file system (%d).", ops->name);
        ret = -ENOSPC;
    }
    else if (ret == 0)
    {
        *empty = ops;
    }

    dfs_unlock();
    return ret;
}

static struct dfs_filesystem* _fs_find(const char *path, int ca)
{
    struct dfs_filesystem *fs = NULL;
    rt_list_t *node, *head;

    head = &_fs_list;
    node = head;
    for (node = head->next; node != head; node = node->next)
    {
        struct dfs_filesystem *e;

        e = rt_list_entry(node, struct dfs_filesystem, list);
        if (ca)
        {
            if (rt_strcmp(path, e->path) == 0)
            {
                fs = e;
                break;
            }
        }
        else
        {
            if (rt_strncmp(path, e->path, rt_strlen(e->path)) == 0)
            {
                fs = e;
            }           
        }
    }

    return fs;
}

/**
 * this function will return the file system mounted on specified path.
 *
 * @param path the specified path string.
 *
 * @return the found file system or NULL if no file system mounted on
 * specified path
 */
struct dfs_filesystem *dfs_filesystem_lookup(const char *path)
{
    return _fs_find(path, 0);
}

static int _fs_new(const struct dfs_filesystem_ops *ops, 
                   const char *path, int rwflag, 
                   const void *data, void *dev, int alcpath)
{
    int ret;
    struct dfs_filesystem *fs;

    if (_fs_find(path, 1))
        return -EEXIST;

    fs = rt_calloc(1, sizeof(*fs));
    if (!fs)
        return -ENOMEM;
    if (alcpath)
        fs->path = rt_strdup(path);
    else
        fs->path = (char*)path;

    if (!fs->path)
    {
        rt_free(fs);
        return -ENOMEM;
    }

    fs->ops = ops;
    fs->dev_id = dev;
    fs->data = (void*)data;
    ret = fs->ops->mount(fs, rwflag, data);
    if (ret != 0)
    {
        if (alcpath)
            rt_free((void*)fs->path);
        rt_free(fs);
    }
    else
    {
        rt_list_init(&fs->list);
        rt_list_insert_after(&_fs_list, &fs->list);
    }

    return ret;
}

/**
 * this function will mount a file system on a specified path.
 *
 * @param device_name the name of device which includes a file system.
 * @param path the path to mount a file system
 * @param filesystemtype the file system type
 * @param rwflag the read/write etc. flag.
 * @param data the private data(parameter) for this file system.
 *
 * @return 0 on successful or -1 on failed.
 */
int dfs_mount(const char   *device_name,
              const char   *path,
              const char   *filesystemtype,
              unsigned long rwflag,
              const void   *data)
{
    const struct dfs_filesystem_ops **ops;
    rt_device_t dev_id;
    int ret;

    /* open specific device */
    if (device_name == NULL)
    {
        /* which is a non-device filesystem mount */
        dev_id = NULL;
    }
    else if ((dev_id = rt_device_find(device_name)) == NULL)
    {
        return -ENODEV;
    }

    /* find out the specific filesystem */
    dfs_lock();

    for (ops = &filesystem_operation_table[0];
           ops < &filesystem_operation_table[DFS_FILESYSTEM_TYPES_MAX]; ops++)
        if ((*ops != NULL) && (strcmp((*ops)->name, filesystemtype) == 0))
            break;

    dfs_unlock();

    if (ops == &filesystem_operation_table[DFS_FILESYSTEM_TYPES_MAX])
    {
        return -ENODEV;
    }

    if (path[1] != '\0')
    {
        struct dfs_fd fd;

        if (dfs_file_open(&fd, path, O_RDONLY | O_DIRECTORY) < 0)
        {
            return -ENOTDIR;
        }
        dfs_file_close(&fd);
    }

    /* open device, but do not check the status of device */
    if (dev_id != NULL)
    {
        if (rt_device_open(dev_id, RT_DEVICE_OFLAG_RDWR) != 0)
        {
            return -EIO;
        }
    }

    ret = _fs_new(*ops, path, 0, data, dev_id, 1);

    return ret;
}

int dfs_pseudo_mount(const char *path, 
                     const struct dfs_filesystem_ops *fsops,
                     const void *data)
{
    int ret;
    const struct dfs_filesystem_ops **ops;

    for (ops = &filesystem_operation_table[0];
           ops < &filesystem_operation_table[DFS_FILESYSTEM_TYPES_MAX]; ops++)
        if ((*ops != NULL) && (*ops == fsops))
            break;

    if (ops == &filesystem_operation_table[DFS_FILESYSTEM_TYPES_MAX])
        return -ENODEV;

    ret = _fs_new(*ops, path, 0, data, 0, 0);

    return ret;
}

/**
 * this function will unmount a file system on specified path.
 *
 * @param specialfile the specified path which mounted a file system.
 *
 * @return 0 on successful or -1 on failed.
 */
int dfs_unmount(const char *specialfile)
{
 #if 0
    char *fullpath;
    struct dfs_filesystem *iter;
    struct dfs_filesystem *fs = NULL;

    fullpath = dfs_normalize_path(NULL, specialfile);
    if (fullpath == NULL)
    {
        rt_set_errno(-ENOTDIR);

        return -1;
    }

    /* lock filesystem */
    dfs_lock();

    for (iter = &filesystem_table[0];
            iter < &filesystem_table[DFS_FILESYSTEMS_MAX]; iter++)
    {
        /* check if the PATH is mounted */
        if ((iter->path != NULL) && (strcmp(iter->path, fullpath) == 0))
        {
            fs = iter;
            break;
        }
    }

    if (fs == NULL ||
        fs->ops->unmount == NULL ||
        fs->ops->unmount(fs) < 0)
    {
        goto err1;
    }

    /* close device, but do not check the status of device */
    if (fs->dev_id != NULL)
        rt_device_close(fs->dev_id);

    if (fs->path != NULL)
        rt_free(fs->path);

    /* clear this filesystem table entry */
    memset(fs, 0, sizeof(struct dfs_filesystem));

    dfs_unlock();
    rt_free(fullpath);

    return 0;

err1:
    dfs_unlock();
    rt_free(fullpath);
#endif
    return -1;
}

/**
 * make a file system on the special device
 *
 * @param fs_name the file system name
 * @param device_name the special device name
 *
 * @return 0 on successful, otherwise failed.
 */
int dfs_mkfs(const char *fs_name, const char *device_name)
{
    int index;
    rt_device_t dev_id = NULL;

    /* check device name, and it should not be NULL */
    if (device_name != NULL)
        dev_id = rt_device_find(device_name);

    if (dev_id == NULL)
    {
        LOG_E("Device (%s) was not found", device_name);
        return -ENODEV;
    }

    /* lock file system */
    dfs_lock();
    /* find the file system operations */
    for (index = 0; index <= DFS_FILESYSTEM_TYPES_MAX; index ++)
    {
        if (filesystem_operation_table[index] != NULL &&
            strcmp(filesystem_operation_table[index]->name, fs_name) == 0)
            break;
    }
    dfs_unlock();

    if (index <= DFS_FILESYSTEM_TYPES_MAX)
    {
        /* find file system operation */
        const struct dfs_filesystem_ops *ops = filesystem_operation_table[index];
        if (ops->mkfs == NULL)
        {
            LOG_E("The file system (%s) mkfs function was not implement", fs_name);
            return -ENOSYS;
        }

        return ops->mkfs(dev_id);
    }

    LOG_E("File system (%s) was not found.", fs_name);

    return -1;
}

/**
 * this function will return the information about a mounted file system.
 *
 * @param path the path which mounted file system.
 * @param buffer the buffer to save the returned information.
 *
 * @return 0 on successful, others on failed.
 */
int dfs_statfs(const char *path, struct statfs *buffer)
{
    struct dfs_filesystem *fs;

    fs = dfs_filesystem_lookup(path);
    if (fs != NULL)
    {
        if (fs->ops->statfs != NULL)
            return fs->ops->statfs(fs, buffer);
    }

    return -1;
}

/* @} */
