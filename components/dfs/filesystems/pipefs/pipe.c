#include <rtthread.h>
#include <rtdevice.h>
#include "pipe.h"

static rt_list_t _pplist = {&_pplist, &_pplist};

rt_pipe_t *rt_pipe_create(const char *name, int bufsz)
{
    rt_pipe_t *pipe;

    RT_ASSERT(bufsz < 0xFFFF);
    pipe = rt_calloc(1, sizeof(rt_pipe_t));
    if (!pipe) 
        return RT_NULL;
    if (name[0] == '/')
        name ++;

    rt_mutex_init(&(pipe->lock), name, RT_IPC_FLAG_FIFO);
    rt_wqueue_init(&(pipe->reader_queue));
    rt_wqueue_init(&(pipe->writer_queue));
    pipe->bufsz = bufsz;
    rt_list_init(&pipe->parent.list);
    rt_strncpy(pipe->parent.name, name, sizeof(pipe->parent.name));
    rt_list_insert_after(&_pplist, &pipe->parent.list);

    return pipe;
}

int rt_pipe_delete(rt_pipe_t *pipe)
{
    rt_list_remove(&(pipe->parent.list));
    rt_mutex_detach(&(pipe->lock));
    rt_ringbuffer_destroy(pipe->fifo);
    rt_free(pipe);

    return 0;
}

rt_pipe_t* rt_pipe_find(const char *name)
{
    rt_list_t *pos, *head;
    rt_pipe_t *ret = 0;

    if (name[0] == '/')
        name ++;

    head = &_pplist;
    rt_list_for_each(pos, head)
    {
        rt_pipe_t *pipe;
        
        pipe = rt_list_entry(pos, rt_pipe_t, parent.list);
        if (rt_strncmp(name, pipe->parent.name, sizeof(pipe->parent.name)) == 0)
        {
            ret = pipe;
            break;
        }
    }

    return ret;
}
