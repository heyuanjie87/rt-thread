/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2017/10/15     bernard      the first version
 */
#include <stdio.h>
#include <stdlib.h>

#include <rtthread.h>

#include "libc.h"

#ifdef RT_USING_PTHREADS
#include <pthread.h>
#endif

int libc_system_init(void)
{
#if defined(RT_USING_DFS) & defined(RT_USING_DFS_DEVFS)
    libc_stdio_set_console(RT_CONSOLE_DEVICE_NAME, O_RDWR);
#endif

#if defined RT_USING_PTHREADS && !defined RT_USING_COMPONENTS_INIT
    pthread_system_init();
#endif

    return 0;
}
INIT_ENV_EXPORT(libc_system_init);
