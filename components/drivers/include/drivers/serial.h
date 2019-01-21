/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-05-15     lgnq         first version.
 * 2012-05-28     bernard      change interfaces
 * 2013-02-20     bernard      use RT_SERIAL_RB_BUFSZ to define
 *                             the size of ring buffer.
 */

#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <rtthread.h>
#include <stdint.h>

#define BAUD_RATE_2400                  2400
#define BAUD_RATE_4800                  4800
#define BAUD_RATE_9600                  9600
#define BAUD_RATE_19200                 19200
#define BAUD_RATE_38400                 38400
#define BAUD_RATE_57600                 57600
#define BAUD_RATE_115200                115200
#define BAUD_RATE_230400                230400
#define BAUD_RATE_460800                460800
#define BAUD_RATE_921600                921600
#define BAUD_RATE_2000000               2000000
#define BAUD_RATE_3000000               3000000

#define DATA_BITS_5                     5
#define DATA_BITS_6                     6
#define DATA_BITS_7                     7
#define DATA_BITS_8                     8
#define DATA_BITS_9                     9

#define STOP_BITS_1                     1
#define STOP_BITS_2                     2
#define STOP_BITS_3                     3
#define STOP_BITS_4                     4

#define UART_PARITY_NONE                0
#define UART_PARITY_ODD                 1
#define UART_PARITY_EVEN                2

#define BIT_ORDER_LSB                   0
#define BIT_ORDER_MSB                   1

#define NRZ_NORMAL                      0       /* Non Return to Zero : normal mode */
#define NRZ_INVERTED                    1       /* Non Return to Zero : inverted mode */

#define RT_SERIAL_EVENT_RX_IND          0x01    /* Rx indication */
#define RT_SERIAL_EVENT_TX_DONE         0x02    /* Tx complete   */
#define RT_SERIAL_EVENT_RX_DMADONE      0x03    /* Rx DMA transfer done */
#define RT_SERIAL_EVENT_TX_DMADONE      0x04    /* Tx DMA transfer done */
#define RT_SERIAL_EVENT_RX_TIMEOUT      0x05    /* Rx timeout    */

#define RT_SERIAL_DMA_RX                0x01
#define RT_SERIAL_DMA_TX                0x02

#define RT_SERIAL_RX_INT                0x01
#define RT_SERIAL_TX_INT                0x02

#define RT_SERIAL_ERR_OVERRUN           0x01
#define RT_SERIAL_ERR_FRAMING           0x02
#define RT_SERIAL_ERR_PARITY            0x03

#define RT_SERIAL_TX_DATAQUEUE_SIZE     2048
#define RT_SERIAL_TX_DATAQUEUE_LWM      30

#define UART_CMD_SET_INTRX    0x10000
#define UART_CMD_SET_INTTX    0x20000

struct serial_configure
{
    uint32_t baud_rate;

    uint8_t data_bits :4;
    uint8_t stop_bits :2;
    uint8_t parity    :2;
    uint8_t c_cc[3];
    uint16_t c_oflag;
};

struct rt_serial_device
{
    struct rt_device          parent;

    const struct rt_uart_ops *ops;
    struct serial_configure   config;

    struct rt_ringbuffer *rxrb;
    struct rt_ringbuffer *txrb;

    rt_wqueue_t rxwq;
    rt_wqueue_t txwq;

    char tx_started;
};
typedef struct rt_serial_device rt_serial_t;

/**
 * uart operators
 */
struct rt_uart_ops
{
    int (*configure)(struct rt_serial_device *serial, struct serial_configure *cfg);
    int (*control)(struct rt_serial_device *serial, int cmd, long arg);

    int (*put)(struct rt_serial_device *serial, char c);
    int (*get)(struct rt_serial_device *serial);

    int (*init)(struct rt_serial_device *serial);
    void (*deinit)(struct rt_serial_device *serial);
};

void rt_hw_serial_isr(struct rt_serial_device *serial, int event);

int rt_hw_serial_register(struct rt_serial_device *serial,
                               const char              *name,
                               int              flag,
                               void                    *data);

#endif
