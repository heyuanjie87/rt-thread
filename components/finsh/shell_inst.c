#include <rtthread.h>
#include "shell.h"

#ifdef FINSH_USING_MSH_DEFAULT
static void finsh_thread_entry(void *p)
{
    struct finsh_shell *shell;

    shell = (struct finsh_shell *)p;
    /* normal is echo mode */
#ifndef FINSH_ECHO_DISABLE_DEFAULT
    shell->echo_mode = 1;
#else
    shell->echo_mode = 0;
#endif
    shell_run(shell);
}

/*
 * @ingroup finsh
 *
 * This function will initialize finsh shell
 */
int finsh_system_init(void)
{
    rt_thread_t tid;
    struct finsh_shell *shell;

    /* create or set shell structure */
    shell = (struct finsh_shell *)rt_calloc(1, sizeof(struct finsh_shell));
    if (!shell)
        return -1;

    tid = rt_thread_create(FINSH_THREAD_NAME,
                           finsh_thread_entry, shell,
                           FINSH_THREAD_STACK_SIZE, 
                           FINSH_THREAD_PRIORITY, 10);

    if (tid)
        rt_thread_startup(tid);

    return 0;
}
INIT_APP_EXPORT(finsh_system_init);
#endif
