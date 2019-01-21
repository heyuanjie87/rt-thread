/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2006-03-13     bernard      first version
 * 2012-05-15     lgnq         modified according bernard's implementation.
 * 2012-05-28     bernard      code cleanup
 * 2012-11-23     bernard      fix compiler warning.
 * 2013-02-20     bernard      use RT_SERIAL_RB_BUFSZ to define
 *                             the size of ring buffer.
 * 2014-07-10     bernard      rewrite serial framework
 * 2014-12-31     bernard      use open_flag for poll_tx stream mode.
 * 2015-05-19     Quintin      fix DMA tx mod tx_dma->activated flag !=RT_FALSE BUG
 *                             in open function.
 * 2015-11-10     bernard      fix the poll rx issue when there is no data.
 * 2016-05-10     armink       add fifo mode to DMA rx when serial->config.bufsz != 0.
 * 2017-01-19     aubr.cool    prevent change serial rx bufsz when serial is opened.
 * 2017-11-07     JasonJia     fix data bits error issue when using tcsetattr.
 * 2017-11-15     JasonJia     fix poll rx issue when data is full.
 *                             add TCFLSH and FIONREAD support.
 * 2018-12-08     Ernest Chen  add DMA choice
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

// #define DEBUG_ENABLE
#define DEBUG_LEVEL         DBG_LOG
#define DBG_SECTION_NAME    "UART"
#define DEBUG_COLOR
#include <rtdbg.h>

#ifdef RT_USING_POSIX
#include <dfs_posix.h>
#include <dfs_poll.h>

#ifdef RT_USING_POSIX_TERMIOS
#include <posix_termios.h>
#endif

#ifndef RT_SERIAL_RB_BUFSZ
#define RT_SERIAL_RB_BUFSZ              32
#endif

static int _uart_init(rt_serial_t *s)
{
    int ret = -EIO;

    if (s->parent.ref_count)
    {
        s->parent.ref_count ++;
        return 0;
    }
    if (!s->ops->init)
        return -ENOSYS;

    s->rxrb = rt_ringbuffer_create(RT_SERIAL_RB_BUFSZ);
    s->txrb = rt_ringbuffer_create(RT_SERIAL_RB_BUFSZ);
    if (!s->rxrb || !s->txrb)
    {
        ret = -ENOMEM;
        goto _free; 
    }

    if (s->ops->init(s) != 0)
        goto _free;

    rt_wqueue_init(&s->txwq);
    rt_wqueue_init(&s->rxwq);

    s->config.baud_rate = 115200;
    s->config.data_bits = DATA_BITS_8;
    s->config.parity = UART_PARITY_NONE;
    s->config.stop_bits = STOP_BITS_1;
    s->tx_started = 0;
#ifdef RT_USING_POSIX_TERMIOS
    s->config.c_oflag = OPOST | ONLCR;
#endif

    if (s->ops->configure(s, &s->config) == 0)
    {
        if (s->ops->control(s, UART_CMD_SET_INTRX, 1) == 0)
        {
            s->parent.ref_count ++;
            return 0;
        }
    }

    if (s->ops->deinit)
        s->ops->deinit(s);
_free:
    rt_ringbuffer_destroy(s->rxrb);
    rt_ringbuffer_destroy(s->txrb);

    return ret;
}

static int _raw_read(rt_serial_t *s, int nb, uint8_t *buf, size_t size)
{
    int ret = 0;
    int c;

    while (1)
    {
        c = rt_ringbuffer_get(s->rxrb, buf, size);
        if (c == 0)
        {
            if (nb && (ret == 0))
            {
                ret = -EAGAIN;
                break;
            }
            if (ret)
                break;

            rt_wqueue_wait(&s->rxwq, 0, -1);            
        }
        else
        {
            size -= c;
            buf += c;
            ret += c;            
        }
    }

    return ret;
}

static int _set_tx_enable(rt_serial_t *s, int en)
{
    int ret = 0;

    if (!s->tx_started && en)
    {
        s->tx_started = 1;
        ret = s->ops->control(s, UART_CMD_SET_INTTX, 1);
    }
    else if (!en)
    {
        ret = s->ops->control(s, UART_CMD_SET_INTTX, 0);
        s->tx_started = 0;
    }

    return ret;
}

static int _raw_write(rt_serial_t *s, int nb, const uint8_t *buf, size_t size)
{
    int ret;
    int c;
    const uint8_t *dbuf;

    dbuf = buf;
    while (1)
    {
        while (size > 0)
        {
            c = rt_ringbuffer_put(s->txrb, dbuf, size);
            if (!c)
                break;
            if (_set_tx_enable(s, 1) != 0)
                return -EIO;
            dbuf += c;
            size -= c;
        }

        if (!size)
            break;
        if (nb)
        {
            ret = -EAGAIN;
            break;
        }
        rt_wqueue_wait(&s->txwq, 0, -1); 
    }

    return (dbuf == buf) ? ret : (dbuf - buf);
}

#ifdef RT_USING_POSIX_TERMIOS
#define O_OPOST(tty)	((tty)->config.c_oflag & OPOST)
#define O_ONLCR(tty)	((tty)->config.c_oflag & ONLCR)

static int _post_write(rt_serial_t *s, int nb, const uint8_t *buf, size_t size)
{
    return 0;
}
#endif

/* fops for serial */
static int serial_fops_open(struct dfs_fd *fd)
{
    int ret;
    rt_serial_t *s;

    s = fd->data;
    ret = _uart_init(s);

    return ret;
}

static int serial_fops_close(struct dfs_fd *fd)
{
    int ret;

    return ret;
}

static int serial_fops_ioctl(struct dfs_fd *fd, int cmd, void *args)
{
    int ret;

    return ret;
}

static int serial_fops_read(struct dfs_fd *file, void *buf, size_t size)
{
    int ret;
    rt_serial_t *s;
    int nb;

    if (size == 0)
        return 0;

    s = file->data;
    nb = file->flags & O_NONBLOCK;

#ifdef SERIAL_THREAD_SAFE
    if (nb)
    {
        if (rt_mutex_take(s->rlock, 0) != 0)
            return -EAGAIN;
    }
    else
    {
        rt_mutex_take(s->rlock, -1);
    }
#endif

    ret = _raw_read(s, nb, (uint8_t*)buf, size);

#ifdef SERIAL_THREAD_SAFE
    rt_mutex_release(s->rlock);
#endif

    return ret;
}

static int serial_fops_write(struct dfs_fd *file, const void *buf, size_t size)
{
    int ret;
    rt_serial_t *s;
    int nb;

    if (size == 0)
        return 0;

    s = file->data;
    nb = file->flags & O_NONBLOCK;

#ifdef SERIAL_THREAD_SAFE
    if (nb)
    {
        if (rt_mutex_take(s->wlock, 0) != 0)
            return -EAGAIN;
    }
    else
    {
        rt_mutex_take(s->wlock, -1);
    }
#endif

#ifdef RT_USING_POSIX_TERMIOS
    if (O_POST(s))
        ret = _post_write(s, nb, (const uint8_t*)buf, size);
    else
#endif
        ret = _raw_write(s, nb, (const uint8_t*)buf, size);

#ifdef SERIAL_THREAD_SAFE
    rt_mutex_release(s->wlock);
#endif

    return ret;
}

static int serial_fops_poll(struct dfs_fd *fd, struct rt_pollreq *req)
{
    int mask = 0;

    return mask;
}

const static struct dfs_file_ops _serial_fops =
{
    serial_fops_open,
    serial_fops_close,
    serial_fops_ioctl,
    serial_fops_read,
    serial_fops_write,
    RT_NULL, /* flush */
    RT_NULL, /* lseek */
    RT_NULL, /* getdents */
    serial_fops_poll,
};

/*
 * serial register
 */
int rt_serial_register(struct rt_serial_device *serial,
                               const char              *name,
                               int              flag,
                               void                    *data)
{
    int ret;
    struct rt_device *device;

    RT_ASSERT(serial != RT_NULL);

    device = &(serial->parent);

    device->type        = RT_Device_Class_Char;
    device->user_data   = data;

    /* register a character device */
    ret = rt_device_register(device, name, flag);

    /* set fops */
    device->fops        = &_serial_fops;

    return ret;
}

/* ISR for serial interrupt */
void rt_serial_isr(struct rt_serial_device *s, int event)
{
    switch (event & 0xff)
    {
    case RT_SERIAL_EVENT_RX_IND:
    {
        uint8_t buf[1];

        while (1)
        {
            int ch = s->ops->get(s);

            if (ch == -1)
                break;
            buf[0] = ch;
            rt_ringbuffer_put(s->rxrb, buf, 1);
        }

        rt_wqueue_wakeup(&s->rxwq, (void*)POLLIN);
    }break;
    case RT_SERIAL_EVENT_TX_DONE:
    {
        uint8_t buf[1];

        if (rt_ringbuffer_get(s->txrb, buf, 1) == 0)
        {
            _set_tx_enable(s, 0);
        }
        else
        {
            s->ops->put(s, buf[0]);
        }

        rt_wqueue_wakeup(&s->txwq, (void*)POLLOUT);        
    }break;
    }
}

#if 0

/*
 * Serial poll routines
 */
rt_inline int _serial_poll_rx(struct rt_serial_device *serial, rt_uint8_t *data, int length)
{
    int ch;
    int size;

    RT_ASSERT(serial != RT_NULL);
    size = length;

    while (length)
    {
        ch = serial->ops->getc(serial);
        if (ch == -1) break;

        *data = ch;
        data ++; length --;

        if (ch == '\n') break;
    }

    return size - length;
}

rt_inline int _serial_poll_tx(struct rt_serial_device *serial, const rt_uint8_t *data, int length)
{
    int size;
    RT_ASSERT(serial != RT_NULL);

    size = length;
    while (length)
    {
        /*
         * to be polite with serial console add a line feed
         * to the carriage return character
         */
        if (*data == '\n' && (serial->parent.open_flag & RT_DEVICE_FLAG_STREAM))
        {
            serial->ops->putc(serial, '\r');
        }

        serial->ops->putc(serial, *data);

        ++ data;
        -- length;
    }

    return size - length;
}

/*
 * Serial interrupt routines
 */
rt_inline int _serial_int_rx(struct rt_serial_device *serial, rt_uint8_t *data, int length)
{
    int size;
    struct rt_serial_rx_fifo* rx_fifo;

    RT_ASSERT(serial != RT_NULL);
    size = length;

    rx_fifo = (struct rt_serial_rx_fifo*) serial->serial_rx;
    RT_ASSERT(rx_fifo != RT_NULL);

    /* read from software FIFO */
    while (length)
    {
        int ch;
        rt_base_t level;

        /* disable interrupt */
        level = rt_hw_interrupt_disable();

        /* there's no data: */
        if ((rx_fifo->get_index == rx_fifo->put_index) && (rx_fifo->is_full == RT_FALSE))
        {
            /* no data, enable interrupt and break out */
            rt_hw_interrupt_enable(level);
            break;
        }

        /* otherwise there's the data: */
        ch = rx_fifo->buffer[rx_fifo->get_index];
        rx_fifo->get_index += 1;
        if (rx_fifo->get_index >= serial->config.bufsz) rx_fifo->get_index = 0;

        if (rx_fifo->is_full == RT_TRUE)
        {
            rx_fifo->is_full = RT_FALSE;
        }

        /* enable interrupt */
        rt_hw_interrupt_enable(level);

        *data = ch & 0xff;
        data ++; length --;
    }

    return size - length;
}

rt_inline int _serial_int_tx(struct rt_serial_device *serial, const rt_uint8_t *data, int length)
{
    int size;
    struct rt_serial_tx_fifo *tx;

    RT_ASSERT(serial != RT_NULL);

    size = length;
    tx = (struct rt_serial_tx_fifo*) serial->serial_tx;
    RT_ASSERT(tx != RT_NULL);

    while (length)
    {
        if (serial->ops->putc(serial, *(char*)data) == -1)
        {
            rt_completion_wait(&(tx->completion), RT_WAITING_FOREVER);
            continue;
        }

        data ++; length --;
    }

    return size - length;
}

#if defined(RT_USING_POSIX) || defined(RT_SERIAL_USING_DMA)
static rt_size_t _serial_fifo_calc_recved_len(struct rt_serial_device *serial)
{
    struct rt_serial_rx_fifo *rx_fifo = (struct rt_serial_rx_fifo *) serial->serial_rx;

    RT_ASSERT(rx_fifo != RT_NULL);

    if (rx_fifo->put_index == rx_fifo->get_index)
    {
        return (rx_fifo->is_full == RT_FALSE ? 0 : serial->config.bufsz);
    }
    else
    {
        if (rx_fifo->put_index > rx_fifo->get_index)
        {
            return rx_fifo->put_index - rx_fifo->get_index;
        }
        else
        {
            return serial->config.bufsz - (rx_fifo->get_index - rx_fifo->put_index);
        }
    }
}
#endif /* RT_USING_POSIX || RT_SERIAL_USING_DMA */

#ifdef RT_SERIAL_USING_DMA
/**
 * Calculate DMA received data length.
 *
 * @param serial serial device
 *
 * @return length
 */
static rt_size_t rt_dma_calc_recved_len(struct rt_serial_device *serial)
{
    return _serial_fifo_calc_recved_len(serial);
}

/**
 * Read data finish by DMA mode then update the get index for receive fifo.
 *
 * @param serial serial device
 * @param len get data length for this operate
 */
static void rt_dma_recv_update_get_index(struct rt_serial_device *serial, rt_size_t len)
{
    struct rt_serial_rx_fifo *rx_fifo = (struct rt_serial_rx_fifo *) serial->serial_rx;

    RT_ASSERT(rx_fifo != RT_NULL);
    RT_ASSERT(len <= rt_dma_calc_recved_len(serial));

    if (rx_fifo->is_full && len != 0) rx_fifo->is_full = RT_FALSE;

    rx_fifo->get_index += len;
    if (rx_fifo->get_index >= serial->config.bufsz)
    {
        rx_fifo->get_index %= serial->config.bufsz;
    }
}

/**
 * DMA received finish then update put index for receive fifo.
 *
 * @param serial serial device
 * @param len received length for this transmit
 */
static void rt_dma_recv_update_put_index(struct rt_serial_device *serial, rt_size_t len)
{
    struct rt_serial_rx_fifo *rx_fifo = (struct rt_serial_rx_fifo *)serial->serial_rx;

    RT_ASSERT(rx_fifo != RT_NULL);

    if (rx_fifo->get_index <= rx_fifo->put_index)
    {
        rx_fifo->put_index += len;
        /* beyond the fifo end */
        if (rx_fifo->put_index >= serial->config.bufsz)
        {
            rx_fifo->put_index %= serial->config.bufsz;
            /* force overwrite get index */
            if (rx_fifo->put_index >= rx_fifo->get_index)
            {
                rx_fifo->is_full = RT_TRUE;
            }
        }
    }
    else
    {
        rx_fifo->put_index += len;
        if (rx_fifo->put_index >= rx_fifo->get_index)
        {
            /* beyond the fifo end */
            if (rx_fifo->put_index >= serial->config.bufsz)
            {
                rx_fifo->put_index %= serial->config.bufsz;
            }
            /* force overwrite get index */
            rx_fifo->is_full = RT_TRUE;
        }
    }
    
    if(rx_fifo->is_full == RT_TRUE) 
    {
        rx_fifo->get_index = rx_fifo->put_index; 
    } 
    
    if (rx_fifo->get_index >= serial->config.bufsz) rx_fifo->get_index = 0;
}

/*
 * Serial DMA routines
 */
rt_inline int _serial_dma_rx(struct rt_serial_device *serial, rt_uint8_t *data, int length)
{
    rt_base_t level;

    RT_ASSERT((serial != RT_NULL) && (data != RT_NULL));

    level = rt_hw_interrupt_disable();

    if (serial->config.bufsz == 0)
    {
        int result = RT_EOK;
        struct rt_serial_rx_dma *rx_dma;

        rx_dma = (struct rt_serial_rx_dma*)serial->serial_rx;
        RT_ASSERT(rx_dma != RT_NULL);

        if (rx_dma->activated != RT_TRUE)
        {
            rx_dma->activated = RT_TRUE;
            RT_ASSERT(serial->ops->dma_transmit != RT_NULL);
            serial->ops->dma_transmit(serial, data, length, RT_SERIAL_DMA_RX);
        }
        else result = -RT_EBUSY;
        rt_hw_interrupt_enable(level);

        if (result == RT_EOK) return length;

        rt_set_errno(result);
        return 0;
    }
    else
    {
        struct rt_serial_rx_fifo *rx_fifo = (struct rt_serial_rx_fifo *) serial->serial_rx;
        rt_size_t recv_len = 0, fifo_recved_len = rt_dma_calc_recved_len(serial);

        RT_ASSERT(rx_fifo != RT_NULL);

        if (length < fifo_recved_len)
            recv_len = length;
        else
            recv_len = fifo_recved_len;

        if (rx_fifo->get_index + recv_len < serial->config.bufsz)
            rt_memcpy(data, rx_fifo->buffer + rx_fifo->get_index, recv_len);
        else
        {
            rt_memcpy(data, rx_fifo->buffer + rx_fifo->get_index,
                    serial->config.bufsz - rx_fifo->get_index);
            rt_memcpy(data + serial->config.bufsz - rx_fifo->get_index, rx_fifo->buffer,
                    recv_len + rx_fifo->get_index - serial->config.bufsz);
        }
        rt_dma_recv_update_get_index(serial, recv_len);
        rt_hw_interrupt_enable(level);
        return recv_len;
    }
}

rt_inline int _serial_dma_tx(struct rt_serial_device *serial, const rt_uint8_t *data, int length)
{
    rt_base_t level;
    rt_err_t result;
    struct rt_serial_tx_dma *tx_dma;

    tx_dma = (struct rt_serial_tx_dma*)(serial->serial_tx);

    result = rt_data_queue_push(&(tx_dma->data_queue), data, length, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        level = rt_hw_interrupt_disable();
        if (tx_dma->activated != RT_TRUE)
        {
            tx_dma->activated = RT_TRUE;
            rt_hw_interrupt_enable(level);

            /* make a DMA transfer */
            serial->ops->dma_transmit(serial, (rt_uint8_t *)data, length, RT_SERIAL_DMA_TX);
        }
        else
        {
            rt_hw_interrupt_enable(level);
        }

        return length;
    }
    else
    {
        rt_set_errno(result);
        return 0;
    }
}
#endif /* RT_SERIAL_USING_DMA */

/* RT-Thread Device Interface */
/*
 * This function initializes serial device.
 */
static rt_err_t rt_serial_init(struct rt_device *dev)
{
    rt_err_t result = RT_EOK;
    struct rt_serial_device *serial;

    RT_ASSERT(dev != RT_NULL);
    serial = (struct rt_serial_device *)dev;

    /* initialize rx/tx */
    serial->serial_rx = RT_NULL;
    serial->serial_tx = RT_NULL;

    /* apply configuration */
    if (serial->ops->configure)
        result = serial->ops->configure(serial, &serial->config);

    return result;
}

static rt_err_t rt_serial_open(struct rt_device *dev, rt_uint16_t oflag)
{
    rt_uint16_t stream_flag = 0;
    struct rt_serial_device *serial;

    RT_ASSERT(dev != RT_NULL);
    serial = (struct rt_serial_device *)dev;

    LOG_D("open serial device: 0x%08x with open flag: 0x%04x",
        dev, oflag);
#ifdef RT_SERIAL_USING_DMA
    /* check device flag with the open flag */
    if ((oflag & RT_DEVICE_FLAG_DMA_RX) && !(dev->flag & RT_DEVICE_FLAG_DMA_RX))
        return -RT_EIO;
    if ((oflag & RT_DEVICE_FLAG_DMA_TX) && !(dev->flag & RT_DEVICE_FLAG_DMA_TX))
        return -RT_EIO;
#endif /* RT_SERIAL_USING_DMA */
    if ((oflag & RT_DEVICE_FLAG_INT_RX) && !(dev->flag & RT_DEVICE_FLAG_INT_RX))
        return -RT_EIO;
    if ((oflag & RT_DEVICE_FLAG_INT_TX) && !(dev->flag & RT_DEVICE_FLAG_INT_TX))
        return -RT_EIO;

    /* keep steam flag */
    if ((oflag & RT_DEVICE_FLAG_STREAM) || (dev->open_flag & RT_DEVICE_FLAG_STREAM))
        stream_flag = RT_DEVICE_FLAG_STREAM;

    /* get open flags */
    dev->open_flag = oflag & 0xff;

    /* initialize the Rx/Tx structure according to open flag */
    if (serial->serial_rx == RT_NULL)
    { 
        if (oflag & RT_DEVICE_FLAG_INT_RX)
        {
            struct rt_serial_rx_fifo* rx_fifo;

            rx_fifo = (struct rt_serial_rx_fifo*) rt_malloc (sizeof(struct rt_serial_rx_fifo) +
                serial->config.bufsz);
            RT_ASSERT(rx_fifo != RT_NULL);
            rx_fifo->buffer = (rt_uint8_t*) (rx_fifo + 1);
            rt_memset(rx_fifo->buffer, 0, serial->config.bufsz);
            rx_fifo->put_index = 0;
            rx_fifo->get_index = 0;
            rx_fifo->is_full = RT_FALSE;

            serial->serial_rx = rx_fifo;
            dev->open_flag |= RT_DEVICE_FLAG_INT_RX;
            /* configure low level device */
            serial->ops->control(serial, RT_DEVICE_CTRL_SET_INT, (void *)RT_DEVICE_FLAG_INT_RX);
        }
#ifdef RT_SERIAL_USING_DMA        
        else if (oflag & RT_DEVICE_FLAG_DMA_RX)
        {
            if (serial->config.bufsz == 0) {
                struct rt_serial_rx_dma* rx_dma;

                rx_dma = (struct rt_serial_rx_dma*) rt_malloc (sizeof(struct rt_serial_rx_dma));
                RT_ASSERT(rx_dma != RT_NULL);
                rx_dma->activated = RT_FALSE;

                serial->serial_rx = rx_dma;
            } else {
                struct rt_serial_rx_fifo* rx_fifo;

                rx_fifo = (struct rt_serial_rx_fifo*) rt_malloc (sizeof(struct rt_serial_rx_fifo) +
                    serial->config.bufsz);
                RT_ASSERT(rx_fifo != RT_NULL);
                rx_fifo->buffer = (rt_uint8_t*) (rx_fifo + 1);
                rt_memset(rx_fifo->buffer, 0, serial->config.bufsz);
                rx_fifo->put_index = 0;
                rx_fifo->get_index = 0;
                rx_fifo->is_full = RT_FALSE;
                serial->serial_rx = rx_fifo;
                /* configure fifo address and length to low level device */
                serial->ops->control(serial, RT_DEVICE_CTRL_CONFIG, (void *) RT_DEVICE_FLAG_DMA_RX);
            }
            dev->open_flag |= RT_DEVICE_FLAG_DMA_RX;
        }
#endif /* RT_SERIAL_USING_DMA */
        else
        {
            serial->serial_rx = RT_NULL;
        }
    }
    else
    {
        if (oflag & RT_DEVICE_FLAG_INT_RX)
            dev->open_flag |= RT_DEVICE_FLAG_INT_RX;
#ifdef RT_SERIAL_USING_DMA
        else if (oflag & RT_DEVICE_FLAG_DMA_RX)
            dev->open_flag |= RT_DEVICE_FLAG_DMA_RX;
#endif /* RT_SERIAL_USING_DMA */  
    }

    if (serial->serial_tx == RT_NULL)
    {
        if (oflag & RT_DEVICE_FLAG_INT_TX)
        {
            struct rt_serial_tx_fifo *tx_fifo;

            tx_fifo = (struct rt_serial_tx_fifo*) rt_malloc(sizeof(struct rt_serial_tx_fifo));
            RT_ASSERT(tx_fifo != RT_NULL);

            rt_completion_init(&(tx_fifo->completion));
            serial->serial_tx = tx_fifo;

            dev->open_flag |= RT_DEVICE_FLAG_INT_TX;
            /* configure low level device */
            serial->ops->control(serial, RT_DEVICE_CTRL_SET_INT, (void *)RT_DEVICE_FLAG_INT_TX);
        }
#ifdef RT_SERIAL_USING_DMA
        else if (oflag & RT_DEVICE_FLAG_DMA_TX)
        {
            struct rt_serial_tx_dma* tx_dma;

            tx_dma = (struct rt_serial_tx_dma*) rt_malloc (sizeof(struct rt_serial_tx_dma));
            RT_ASSERT(tx_dma != RT_NULL);
            tx_dma->activated = RT_FALSE;

            rt_data_queue_init(&(tx_dma->data_queue), 8, 4, RT_NULL);
            serial->serial_tx = tx_dma;

            dev->open_flag |= RT_DEVICE_FLAG_DMA_TX;
        }
#endif /* RT_SERIAL_USING_DMA */
        else
        {
            serial->serial_tx = RT_NULL;
        }
    }
    else
    {
        if (oflag & RT_DEVICE_FLAG_INT_TX)
            dev->open_flag |= RT_DEVICE_FLAG_INT_TX;
#ifdef RT_SERIAL_USING_DMA
        else if (oflag & RT_DEVICE_FLAG_DMA_TX)
            dev->open_flag |= RT_DEVICE_FLAG_DMA_TX;
#endif /* RT_SERIAL_USING_DMA */    
    }

    /* set stream flag */
    dev->open_flag |= stream_flag;

    return RT_EOK;
}

static rt_err_t rt_serial_close(struct rt_device *dev)
{
    struct rt_serial_device *serial;

    RT_ASSERT(dev != RT_NULL);
    serial = (struct rt_serial_device *)dev;

    /* this device has more reference count */
    if (dev->ref_count > 1) return RT_EOK;

    if (dev->open_flag & RT_DEVICE_FLAG_INT_RX)
    {
        struct rt_serial_rx_fifo* rx_fifo;

        rx_fifo = (struct rt_serial_rx_fifo*)serial->serial_rx;
        RT_ASSERT(rx_fifo != RT_NULL);

        rt_free(rx_fifo);
        serial->serial_rx = RT_NULL;
        dev->open_flag &= ~RT_DEVICE_FLAG_INT_RX;
        /* configure low level device */
        serial->ops->control(serial, RT_DEVICE_CTRL_CLR_INT, (void*)RT_DEVICE_FLAG_INT_RX);
    }
#ifdef RT_SERIAL_USING_DMA
    else if (dev->open_flag & RT_DEVICE_FLAG_DMA_RX)
    {
        if (serial->config.bufsz == 0) {
            struct rt_serial_rx_dma* rx_dma;

            rx_dma = (struct rt_serial_rx_dma*)serial->serial_rx;
            RT_ASSERT(rx_dma != RT_NULL);

            rt_free(rx_dma);
        } else {
            struct rt_serial_rx_fifo* rx_fifo;

            rx_fifo = (struct rt_serial_rx_fifo*)serial->serial_rx;
            RT_ASSERT(rx_fifo != RT_NULL);

            rt_free(rx_fifo);
        }
        /* configure low level device */
        serial->ops->control(serial, RT_DEVICE_CTRL_CLR_INT, (void *) RT_DEVICE_FLAG_DMA_RX);
        serial->serial_rx = RT_NULL;
        dev->open_flag &= ~RT_DEVICE_FLAG_DMA_RX;
    }
#endif /* RT_SERIAL_USING_DMA */
    
    if (dev->open_flag & RT_DEVICE_FLAG_INT_TX)
    {
        struct rt_serial_tx_fifo* tx_fifo;

        tx_fifo = (struct rt_serial_tx_fifo*)serial->serial_tx;
        RT_ASSERT(tx_fifo != RT_NULL);

        rt_free(tx_fifo);
        serial->serial_tx = RT_NULL;
        dev->open_flag &= ~RT_DEVICE_FLAG_INT_TX;
        /* configure low level device */
        serial->ops->control(serial, RT_DEVICE_CTRL_CLR_INT, (void*)RT_DEVICE_FLAG_INT_TX);
    }
#ifdef RT_SERIAL_USING_DMA
    else if (dev->open_flag & RT_DEVICE_FLAG_DMA_TX)
    {
        struct rt_serial_tx_dma* tx_dma;

        tx_dma = (struct rt_serial_tx_dma*)serial->serial_tx;
        RT_ASSERT(tx_dma != RT_NULL);

        rt_free(tx_dma);
        serial->serial_tx = RT_NULL;
        dev->open_flag &= ~RT_DEVICE_FLAG_DMA_TX;
    }
#endif /* RT_SERIAL_USING_DMA */
    return RT_EOK;
}

static rt_size_t rt_serial_read(struct rt_device *dev,
                                rt_off_t          pos,
                                void             *buffer,
                                rt_size_t         size)
{
    struct rt_serial_device *serial;

    RT_ASSERT(dev != RT_NULL);
    if (size == 0) return 0;

    serial = (struct rt_serial_device *)dev;

    if (dev->open_flag & RT_DEVICE_FLAG_INT_RX)
    {
        return _serial_int_rx(serial, buffer, size);
    }
#ifdef RT_SERIAL_USING_DMA
    else if (dev->open_flag & RT_DEVICE_FLAG_DMA_RX)
    {
        return _serial_dma_rx(serial, buffer, size);
    }
#endif /* RT_SERIAL_USING_DMA */    

    return _serial_poll_rx(serial, buffer, size);
}

static rt_size_t rt_serial_write(struct rt_device *dev,
                                 rt_off_t          pos,
                                 const void       *buffer,
                                 rt_size_t         size)
{
    struct rt_serial_device *serial;

    RT_ASSERT(dev != RT_NULL);
    if (size == 0) return 0;

    serial = (struct rt_serial_device *)dev;

    if (dev->open_flag & RT_DEVICE_FLAG_INT_TX)
    {
        return _serial_int_tx(serial, buffer, size);
    }
#ifdef RT_SERIAL_USING_DMA    
    else if (dev->open_flag & RT_DEVICE_FLAG_DMA_TX)
    {
        return _serial_dma_tx(serial, buffer, size);
    }
#endif /* RT_SERIAL_USING_DMA */
    else
    {
        return _serial_poll_tx(serial, buffer, size);
    }
}

#ifdef RT_USING_POSIX_TERMIOS
struct speed_baudrate_item
{
    speed_t speed;
    int baudrate;
};

const static struct speed_baudrate_item _tbl[] =
{
    {B2400, BAUD_RATE_2400},
    {B4800, BAUD_RATE_4800},
    {B9600, BAUD_RATE_9600},
    {B19200, BAUD_RATE_19200},
    {B38400, BAUD_RATE_38400},
    {B57600, BAUD_RATE_57600},
    {B115200, BAUD_RATE_115200},
    {B230400, BAUD_RATE_230400},
    {B460800, BAUD_RATE_460800},
    {B921600, BAUD_RATE_921600},
    {B2000000, BAUD_RATE_2000000},
    {B3000000, BAUD_RATE_3000000},
};

static speed_t _get_speed(int baudrate)
{
    int index;

    for (index = 0; index < sizeof(_tbl)/sizeof(_tbl[0]); index ++)
    {
        if (_tbl[index].baudrate == baudrate)
            return _tbl[index].speed;
    }

    return B0;
}

static int _get_baudrate(speed_t speed)
{
    int index;

    for (index = 0; index < sizeof(_tbl)/sizeof(_tbl[0]); index ++)
    {
        if (_tbl[index].speed == speed)
            return _tbl[index].baudrate;
    }

    return 0;
}

static void _tc_flush(struct rt_serial_device *serial, int queue)
{
    int ch = -1;
    struct rt_serial_rx_fifo *rx_fifo = RT_NULL;
    struct rt_device *device = RT_NULL;

    RT_ASSERT(serial != RT_NULL);

    device = &(serial->parent);
    rx_fifo = (struct rt_serial_rx_fifo *) serial->serial_rx;

    switch(queue)
    {
        case TCIFLUSH:
        case TCIOFLUSH:

            RT_ASSERT(rx_fifo != RT_NULL);

            if((device->open_flag & RT_DEVICE_FLAG_INT_RX) || (device->open_flag & RT_DEVICE_FLAG_DMA_RX))
            {
                RT_ASSERT(RT_NULL != rx_fifo);
                rt_memset(rx_fifo->buffer, 0, serial->config.bufsz);
                rx_fifo->put_index = 0;
                rx_fifo->get_index = 0;
                rx_fifo->is_full = RT_FALSE;
            }
            else
            {
                while (1)
                {
                    ch = serial->ops->getc(serial);
                    if (ch == -1) break;
                }
            }

            break;

         case TCOFLUSH:
            break;
    }

}

#endif

static rt_err_t rt_serial_control(struct rt_device *dev,
                                  int              cmd,
                                  void             *args)
{
    rt_err_t ret = RT_EOK;
    struct rt_serial_device *serial;

    RT_ASSERT(dev != RT_NULL);
    serial = (struct rt_serial_device *)dev;

    switch (cmd)
    {
        case RT_DEVICE_CTRL_SUSPEND:
            /* suspend device */
            dev->flag |= RT_DEVICE_FLAG_SUSPENDED;
            break;

        case RT_DEVICE_CTRL_RESUME:
            /* resume device */
            dev->flag &= ~RT_DEVICE_FLAG_SUSPENDED;
            break;

        case RT_DEVICE_CTRL_CONFIG:
            if (args)
            {
                struct serial_configure *pconfig = (struct serial_configure *) args;
                if (pconfig->bufsz != serial->config.bufsz && serial->parent.ref_count)
                {
                    /*can not change buffer size*/
                    return RT_EBUSY;
                }
                /* set serial configure */
                serial->config = *pconfig;
                if (serial->parent.ref_count)
                {
                    /* serial device has been opened, to configure it */
                    serial->ops->configure(serial, (struct serial_configure *) args);
                }
            }

            break;

#ifdef RT_USING_POSIX_TERMIOS
        case TCGETA:
            {
                struct termios *tio = (struct termios*)args;
                if (tio == RT_NULL) return -RT_EINVAL;

                tio->c_iflag = 0;
                tio->c_oflag = 0;
                tio->c_lflag = 0;

                /* update oflag for console device */
                if (rt_console_get_device() == dev)
                    tio->c_oflag = OPOST | ONLCR;

                /* set cflag */
                tio->c_cflag = 0;
                if (serial->config.data_bits == DATA_BITS_5)
                    tio->c_cflag = CS5;
                else if (serial->config.data_bits == DATA_BITS_6)
                    tio->c_cflag = CS6;
                else if (serial->config.data_bits == DATA_BITS_7)
                    tio->c_cflag = CS7;
                else if (serial->config.data_bits == DATA_BITS_8)
                    tio->c_cflag = CS8;

                if (serial->config.stop_bits == STOP_BITS_2)
                    tio->c_cflag |= CSTOPB;

                if (serial->config.parity == PARITY_EVEN)
                    tio->c_cflag |= PARENB;
                else if (serial->config.parity == PARITY_ODD)
                    tio->c_cflag |= (PARODD | PARENB);

                cfsetospeed(tio, _get_speed(serial->config.baud_rate));
            }
            break;

        case TCSETAW:
        case TCSETAF:
        case TCSETA:
            {
                int baudrate;
                struct serial_configure config;

                struct termios *tio = (struct termios*)args;
                if (tio == RT_NULL) return -RT_EINVAL;

                config = serial->config;

                baudrate = _get_baudrate(cfgetospeed(tio));
                config.baud_rate = baudrate;

                switch (tio->c_cflag & CSIZE)
                {
                case CS5:
                    config.data_bits = DATA_BITS_5;
                    break;
                case CS6:
                    config.data_bits = DATA_BITS_6;
                    break;
                case CS7:
                    config.data_bits = DATA_BITS_7;
                    break;
                default:
                    config.data_bits = DATA_BITS_8;
                    break;
                }

                if (tio->c_cflag & CSTOPB) config.stop_bits = STOP_BITS_2;
                else config.stop_bits = STOP_BITS_1;

                if (tio->c_cflag & PARENB)
                {
                    if (tio->c_cflag & PARODD) config.parity = PARITY_ODD;
                    else config.parity = PARITY_EVEN;
                }
                else config.parity = PARITY_NONE;

                serial->ops->configure(serial, &config);
            }
            break;
        case TCFLSH:
            {
                int queue = (int)args;

                _tc_flush(serial, queue);
            }

            break;
        case TCXONC:
            break;
#endif
#ifdef RT_USING_POSIX
        case FIONREAD:
            {
                rt_size_t recved = 0;
                rt_base_t level;

                level = rt_hw_interrupt_disable();
                recved = _serial_fifo_calc_recved_len(serial);
                rt_hw_interrupt_enable(level);

                *(rt_size_t *)args = recved;
            }
            break;
#endif
        default :
            /* control device */
            ret = serial->ops->control(serial, cmd, args);
            break;
    }

    return ret;
}

#endif

#endif
