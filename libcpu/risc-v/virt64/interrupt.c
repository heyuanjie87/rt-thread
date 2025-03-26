/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018/10/01     Bernard      The first version
 * 2018/12/27     Jesven       Change irq enable/disable to cpu0
 */
#include <rtthread.h>
#include <plic.h>
#include <rthw.h>
#include "encoding.h"
#include "riscv.h"
#include "interrupt.h"

#ifdef RT_USING_SMART
#include <ioremap.h>
#endif

static struct rt_irq_desc isr_table[INTERRUPTS_MAX];
static struct plic_handler plic_handlers[1];

rt_inline struct plic_handler *plic_handler_get(void)
{
    return &plic_handlers[0];
}

static void plic_init(void)
{
    void *plic_base = (void *)0x0c000000L;

#ifdef RT_USING_SMART
    // PLIC takes up 64 MB space
    plic_base = rt_ioremap(plic_base, 64 * 1024 * 1024);
#endif

    plic_handler_init(plic_handler_get(), plic_base, 1);

    plic_set_threshold(plic_handler_get(), 0);
}

static rt_isr_handler_t rt_hw_interrupt_handle(rt_uint32_t vector, void *param)
{
    rt_kprintf("UN-handled interrupt %d occurred!!!\n", vector);
    return RT_NULL;
}

/**
 * This function will mask a interrupt.
 * @param vector the interrupt number
 */
void rt_hw_interrupt_mask(int vector)
{
    if ((vector < 0) || (vector > IRQ_MAX_NR))
    {
        return;
    }

    plic_irq_disable(plic_handler_get(), vector);
}

/**
 * This function will un-mask a interrupt.
 * @param vector the interrupt number
 */
void rt_hw_interrupt_umask(int vector)
{
    if ((vector < 0) || (vector > IRQ_MAX_NR))
    {
        return;
    }

    plic_set_priority(plic_handler_get(), vector, 1);

    plic_irq_enable(plic_handler_get(), vector);
}

/**
 * This function will install a interrupt service routine to a interrupt.
 * @param vector the interrupt number
 * @param new_handler the interrupt service routine to be installed
 * @param old_handler the old interrupt service routine
 */
rt_isr_handler_t rt_hw_interrupt_install(int vector, rt_isr_handler_t handler,
                                         void *param, const char *name)
{
    rt_isr_handler_t old_handler = RT_NULL;

    if ((vector < 0) || (vector > IRQ_MAX_NR))
    {
        return old_handler;
    }

    old_handler = isr_table[IRQ_OFFSET + vector].handler;

    isr_table[IRQ_OFFSET + vector].handler = handler;
    isr_table[IRQ_OFFSET + vector].param = param;

    return old_handler;
}

void rt_hw_interrupt_init(void)
{
    /* init exceptions table */
    for (int idx = 0; idx < INTERRUPTS_MAX; idx++)
    {
        isr_table[idx].handler = (rt_isr_handler_t)rt_hw_interrupt_handle;
        isr_table[idx].param   = RT_NULL;
    }

    plic_init();

    /* Enable supervisor external interrupts. */
    set_csr(sie, SIE_SEIE);
}

/*
 * Handling an interrupt is a two-step process: first you claim the interrupt
 * by reading the claim register, then you complete the interrupt by writing
 * that source ID back to the same claim register.  This automatically enables
 * and disables the interrupt, so there's nothing else to do.
 */
void handle_irq(void)
{
    struct plic_handler *plic;
    rt_isr_handler_t isr;
    void *param;
    int irq;

    plic = plic_handler_get();

    while ((irq = plic_claim(plic)))
    {
        isr = isr_table[IRQ_OFFSET + irq].handler;
        param = isr_table[IRQ_OFFSET + irq].param;
        if (isr)
        {
            isr(irq, param);
        }

        plic_complete(plic, irq);
    }
}
