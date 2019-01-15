/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2013-03-30     Bernard      the first verion for FinSH
 * 2015-08-28     Bernard      Add mkfs command.
 */

#include <rtthread.h>

#ifdef FINSH_USING_MSH

#include <finsh.h>
#include "msh.h"

#ifdef RT_USING_DFS
#include <dfs_posix.h>

#ifdef DFS_USING_WORKDIR
extern char working_directory[];
#endif

int cmd_mv(int argc, char **argv)
{
    if (argc != 3)
    {
        rt_kprintf("Usage: mv SOURCE DEST\n");
        rt_kprintf("Rename SOURCE to DEST, or move SOURCE(s) to DIRECTORY.\n");
    }
    else
    {
        int fd;
        char *dest = RT_NULL;

        rt_kprintf("%s => %s\n", argv[1], argv[2]);

        fd = open(argv[2], O_DIRECTORY, 0);
        if (fd >= 0)
        {
            char *src;

            close(fd);

            /* it's a directory */
            dest = (char *)rt_malloc(DFS_PATH_MAX);
            if (dest == RT_NULL)
            {
                rt_kprintf("out of memory\n");
                return -RT_ENOMEM;
            }

            src = argv[1] + rt_strlen(argv[1]);
            while (src != argv[1])
            {
                if (*src == '/') break;
                src --;
            }

            rt_snprintf(dest, DFS_PATH_MAX - 1, "%s/%s", argv[2], src);
        }
        else
        {
            fd = open(argv[2], O_RDONLY, 0);
            if (fd >= 0)
            {
                close(fd);

                unlink(argv[2]);
            }

            dest = argv[2];
        }

        rename(argv[1], dest);
        if (dest != RT_NULL && dest != argv[2]) rt_free(dest);
    }

    return 0;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_mv, __cmd_mv, Rename SOURCE to DEST.);

#ifdef DFS_USING_WORKDIR
int cmd_cd(int argc, char **argv)
{
    if (argc == 1)
    {
        rt_kprintf("%s\n", working_directory);
    }
    else if (argc == 2)
    {
        if (chdir(argv[1]) != 0)
        {
            rt_kprintf("No such directory: %s\n", argv[1]);
        }
    }

    return 0;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_cd, __cmd_cd, Change the shell working directory.);

int cmd_pwd(int argc, char **argv)
{
    rt_kprintf("%s\n", working_directory);
    return 0;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_pwd, __cmd_pwd, Print the name of the current working directory.);
#endif


#endif

#ifdef RT_USING_LWIP
int cmd_ifconfig(int argc, char **argv)
{
    extern void list_if(void);
    extern void set_if(char *netif_name, char *ip_addr, char *gw_addr, char *nm_addr);


    if (argc == 1)
    {
        list_if();
    }
    else if (argc == 5)
    {
        rt_kprintf("config : %s\n", argv[1]);
        rt_kprintf("IP addr: %s\n", argv[2]);
        rt_kprintf("Gateway: %s\n", argv[3]);
        rt_kprintf("netmask: %s\n", argv[4]);
        set_if(argv[1], argv[2], argv[3], argv[4]);
    }
    else
    {
        rt_kprintf("bad parameter! e.g: ifconfig e0 192.168.1.30 192.168.1.1 255.255.255.0\n");
    }

    return 0;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_ifconfig, __cmd_ifconfig, list the information of network interfaces);

#ifdef RT_LWIP_DNS
#include <lwip/api.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/init.h>

int cmd_dns(int argc, char **argv)
{
    extern void set_dns(char* dns_server);

    if (argc == 1)
    {
        int index;

#if (LWIP_VERSION) < 0x02000000U
        ip_addr_t ip_addr;
        for(index=0; index<DNS_MAX_SERVERS; index++)
        {
            ip_addr = dns_getserver(index);
            rt_kprintf("dns server #%d: %s\n", index, ipaddr_ntoa(&ip_addr));
        }
#else
        const ip_addr_t *ip_addr;
        for(index=0; index<DNS_MAX_SERVERS; index++)
        {
            ip_addr = dns_getserver(index);
            rt_kprintf("dns server #%d: %s\n", index, ipaddr_ntoa(ip_addr));
        }
#endif
    }
    else if (argc == 2)
    {
        rt_kprintf("dns : %s\n", argv[1]);
        set_dns(argv[1]);
    }
    else
    {
        rt_kprintf("bad parameter! e.g: dns 114.114.114.114\n");
    }
    return 0;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_dns, __cmd_dns, list the information of dns);
#endif

#if defined (RT_LWIP_TCP) || defined (RT_LWIP_UDP)
int cmd_netstat(int argc, char **argv)
{
    extern void list_tcps(void);
    extern void list_udps(void);

#ifdef RT_LWIP_TCP
    list_tcps();
#endif
#ifdef RT_LWIP_UDP
    list_udps();
#endif

    return 0;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_netstat, __cmd_netstat, list the information of TCP / IP);
#endif
#endif /* RT_USING_LWIP */


#ifdef RT_USING_HEAP
int cmd_free(int argc, char **argv)
{
    extern void list_mem(void);
    extern void list_memheap(void);

#ifdef RT_USING_MEMHEAP_AS_HEAP
    list_memheap();
#else
    list_mem();
#endif
    return 0;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_free, __cmd_free, Show the memory usage in the system.);
#endif

#endif /* FINSH_USING_MSH */
