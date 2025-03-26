/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-05-20     bigmagic     first version
 * 2022-09-16     WangXiaoyao  Porting to rv64
 */
#include <rthw.h>
#include <rtthread.h>
#include <stdint.h>
#include "plic.h"
#include <riscv_io.h>
#include "encoding.h"
#include <interrupt.h>

#include <riscv.h>
#include <string.h>
#include <stdlib.h>

#ifdef RT_USING_SMART
#include <ioremap.h>
#else
#define rt_ioremap(addr, ...) (addr)
#endif

size_t plic_base = 0x0c000000L;

/*
 * Each interrupt source has a priority register associated with it.
 * We always hardwire it to one in Linux.
 */
#define PRIORITY_BASE 0
#define PRIORITY_PER_ID 4

/*
 * Each hart context has a vector of interrupt enable bits associated with it.
 * There's one bit for each interrupt source.
 */
#define CONTEXT_ENABLE_BASE 0x2000
#define CONTEXT_ENABLE_SIZE 0x80

/*
 * Each hart context has a set of control registers associated with it.  Right
 * now there's only two: a source priority threshold over which the hart will
 * take an interrupt, and a register to claim interrupts.
 */
#define CONTEXT_BASE 0x200000
#define CONTEXT_SIZE 0x1000
#define CONTEXT_THRESHOLD 0x00
#define CONTEXT_CLAIM 0x04

static struct plic_handler plic_handlers[1];

static struct plic_handler* plic_handler_get(void)
{
    return &plic_handlers[0];
}

static void plic_toggle(struct plic_handler *handler, uint32_t hwirq, int enable)
{
    uint32_t *reg = handler->enable_base + (hwirq / 32) * sizeof(uint32_t);
    uint32_t hwirq_mask = 1 << (hwirq % 32);

    if (enable)
        writel(readl(reg) | hwirq_mask, reg);
    else
        writel(readl(reg) & ~hwirq_mask, reg);
}

/*
 * Each PLIC interrupt source can be assigned a priority by writing
 * to its 32-bit memory-mapped priority register.
 * The QEMU-virt (the same as FU540-C000) supports 7 levels of priority.
 * A priority value of 0 is reserved to mean "never interrupt" and
 * effectively disables the interrupt.
 * Priority 1 is the lowest active priority, and priority 7 is the highest.
 * Ties between global interrupts of the same priority are broken by
 * the Interrupt ID; interrupts with the lowest ID have the highest
 * effective priority.
 */
void plic_set_priority(int irq, int priority)
{
    struct plic_handler *handler = plic_handler_get();

    writel(priority, handler->base + PRIORITY_BASE + irq * PRIORITY_PER_ID);
}

/*
 * Each global interrupt can be enabled by setting the corresponding
 * bit in the enables registers.
 */
void plic_irq_enable(int irq)
{
    struct plic_handler *handler = plic_handler_get();
    
    plic_toggle(handler, irq, 1);
}

void plic_irq_disable(int irq)
{
    struct plic_handler *handler = plic_handler_get();
    
    plic_toggle(handler, irq, 1);
}

/*
 * PLIC will mask all interrupts of a priority less than or equal to threshold.
 * Maximum threshold is 7.
 * For example, a threshold value of zero permits all interrupts with
 * non-zero priority, whereas a value of 7 masks all interrupts.
 * Notice, the threshold is global for PLIC, not for each interrupt source.
 */
void plic_set_threshold(int threshold)
{
    int hart = __raw_hartid();
    *(uint32_t *)PLIC_THRESHOLD(hart) = threshold;
}

/*
 * DESCRIPTION:
 *    Query the PLIC what interrupt we should serve.
 *    Perform an interrupt claim by reading the claim register, which
 *    returns the ID of the highest-priority pending interrupt or zero if there
 *    is no pending interrupt.
 *    A successful claim also atomically clears the corresponding pending bit
 *    on the interrupt source.
 * RETURN VALUE:
 *    the ID of the highest-priority pending interrupt or zero if there
 *    is no pending interrupt.
 */
int plic_claim(void)
{
    struct plic_handler *handler = plic_handler_get();
    void *claim = handler->hart_base + CONTEXT_CLAIM;

    return readl(claim);
}

/*
 * DESCRIPTION:
 *    Writing the interrupt ID it received from the claim (irq) to the
 *    complete register would signal the PLIC we've served this IRQ.
 *    The PLIC does not check whether the completion ID is the same as the
 *    last claim ID for that target. If the completion ID does not match an
 *    interrupt source that is currently enabled for the target, the completion
 *    is silently ignored.
 * RETURN VALUE: none
 */
void plic_complete(int irq)
{
    struct plic_handler *handler = plic_handler_get();
    void *claim = handler->hart_base + CONTEXT_CLAIM;

    writel(irq, claim);
}

void plic_set_ie(rt_uint32_t word_index, rt_uint32_t val)
{
    volatile void *plic_ie = (void *)(rt_ubase_t)(plic_base + PLIC_ENABLE_BASE + 0x80 + word_index * 4);
    writel(val, plic_ie);
}

static void _set_sie(int hartid)
{
    for (size_t i = hartid * WORD_CNT_BYTE; i < 32; i++)
        plic_set_ie(i, 0xffffffff);
}

void plic_handler_init(struct plic_handler *handler, void *base, uint32_t context_id)
{
    handler->base = base;
    handler->hart_base = base + CONTEXT_BASE + context_id * CONTEXT_SIZE;
    handler->enable_base = base + CONTEXT_ENABLE_BASE + context_id * CONTEXT_ENABLE_SIZE;
}

void plic_init()
{
    // PLIC takes up 64 MB space
    plic_base = (size_t)rt_ioremap((void *)plic_base, 64 * 1024 * 1024);

    plic_handler_init(plic_handler_get(), plic_base, 1);

   /// plic_set_threshold(0);

    for (int i = 0; i < CONFIG_IRQ_NR; i++)
    {
        plic_set_priority(i, 1);
    }
}
