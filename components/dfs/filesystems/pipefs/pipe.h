/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 */
#ifndef PIPE_H__
#define PIPE_H__

/**
 * Pipe 
 */
#include <rtthread.h>
#include <stdint.h>

#ifndef RT_PIPE_BUFSZ
#define PIPE_BUFSZ    512
#else
#define PIPE_BUFSZ    RT_PIPE_BUFSZ
#endif

struct rt_pipe_device
{
    struct rt_object parent;

    /* ring buffer in pipe device */
    struct rt_ringbuffer *fifo;
    uint16_t bufsz;

    uint8_t readers;
    uint8_t writers;
    uint32_t ref_count;

    rt_wqueue_t reader_queue;
    rt_wqueue_t writer_queue;

    struct rt_mutex lock;
};
typedef struct rt_pipe_device rt_pipe_t;

rt_pipe_t* rt_pipe_find(const char *name);
rt_pipe_t *rt_pipe_create(const char *name, int bufsz);
int rt_pipe_delete(rt_pipe_t *pipe);

#endif /* PIPE_H__ */
