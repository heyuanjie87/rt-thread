#include <rtthread.h>
#include <dfs_posix.h>
#include <stdio.h>

static void tpcd(int argc, char **argv)
{
    int fd;
    const char *name = ":pipe/123";

    fd = open(name, O_RDWR | O_CREAT);
    if (fd < 0)
    {
        printf("create pipe fail\n");
        return;
    }
    close(fd);
    unlink(name);
}
MSH_CMD_EXPORT(tpcd, test pipe create&delete);
