/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2011-06-02     Bernard      Add finsh_get_prompt function declaration
 */

#ifndef __SHELL_H__
#define __SHELL_H__

#include <rtthread.h>
#include "finsh.h"

#ifndef FINSH_THREAD_PRIORITY
#define FINSH_THREAD_PRIORITY 20
#endif
#ifndef FINSH_THREAD_STACK_SIZE
#define FINSH_THREAD_STACK_SIZE 2048
#endif
#ifndef FINSH_CMD_SIZE
#define FINSH_CMD_SIZE      80
#endif

#define FINSH_OPTION_ECHO   0x01

#define FINSH_PROMPT        finsh_get_prompt()
const char* finsh_get_prompt(void);
int finsh_set_prompt(const char * prompt);

#ifdef FINSH_USING_HISTORY
    #ifndef FINSH_HISTORY_LINES
        #define FINSH_HISTORY_LINES 5
    #endif
#endif

#ifdef FINSH_USING_AUTH
    #ifndef FINSH_PASSWORD_MAX
        #define FINSH_PASSWORD_MAX RT_NAME_MAX
    #endif
    #ifndef FINSH_PASSWORD_MIN
        #define FINSH_PASSWORD_MIN 6
    #endif
    #ifndef FINSH_DEFAULT_PASSWORD
        #define FINSH_DEFAULT_PASSWORD "rtthread"
    #endif
#endif /* FINSH_USING_AUTH */

#ifndef FINSH_THREAD_NAME
#define FINSH_THREAD_NAME   "tshell"
#endif

enum input_stat
{
    WAIT_NORMAL,
    WAIT_SPEC_KEY,
    WAIT_FUNC_KEY,
};

struct finsh_shell
{
    enum input_stat stat;

    unsigned char echo_mode:1;
    unsigned char prompt_mode: 1;

#ifdef FINSH_USING_HISTORY
    short current_history;
    short history_count;
    char cmd_history[FINSH_HISTORY_LINES][FINSH_CMD_SIZE];
#endif

    char line[FINSH_CMD_SIZE];
    unsigned char line_position;
    unsigned char line_curpos;

#ifdef FINSH_USING_AUTH
    char password[FINSH_PASSWORD_MAX];
#endif
};

void shell_run(struct finsh_shell *shell);

#endif
