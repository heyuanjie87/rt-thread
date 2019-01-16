#include <rtthread.h>
#include <dfs_posix.h>
#include <stdio.h>
#include <stdlib.h>

static void _cat(const char* filename)
{
    int length;
    char buffer[81];
    int fd;

    fd = open(filename, O_RDONLY, 0);
    if (fd < 0)
    {
        printf("Open %s failed\n", filename);
        return;
    }

    do
    {
        memset(buffer, 0, sizeof(buffer));
        length = read(fd, buffer, sizeof(buffer)-1);
        if (length > 0)
        {
            printf("%s", buffer);
        }
    }while (length > 0);

    close(fd);
}

static int cat(int argc, char **argv)
{
    int index;

    if (argc == 1)
    {
        printf("Usage: cat [FILE]...\n");
        printf("Concatenate FILE(s)\n");
        return 0;
    }

    for (index = 1; index < argc; index ++)
    {
        _cat(argv[index]);
    }

    return 0;
}
MSH_CMD_EXPORT(cat, Concatenate FILE(s));

static int rm(int argc, char **argv)
{
    int index;

    if (argc == 1)
    {
        printf("Usage: rm FILE...\n");
        printf("Remove (unlink) the FILE(s).\n");
        return 0;
    }

    for (index = 1; index < argc; index ++)
    {
        unlink(argv[index]);
    }

    return 0;
}
MSH_CMD_EXPORT(rm, Remove(unlink) the FILE(s).);

static int echo(int argc, char** argv)
{
    if (argc == 2)
    {
        printf("%s\n", argv[1]);
    }
    else if (argc == 3)
    {
        int fd;

        fd = open(argv[2], O_RDWR | O_APPEND | O_CREAT, 0);
        if (fd >= 0)
        {
            write (fd, argv[1], strlen(argv[1]));
            close(fd);
        }
        else
        {
            printf("open file:%s failed!\n", argv[2]);
        }
    }
    else
    {
        printf("Usage: echo \"string\" [filename]\n");
    }

    return 0;
}
MSH_CMD_EXPORT(echo, echo string to file);

static int cmd_mkdir(int argc, char **argv)
{
    if (argc == 1)
    {
        rt_kprintf("Usage: mkdir [OPTION] DIRECTORY\n");
        rt_kprintf("Create the DIRECTORY, if they do not already exist.\n");
    }
    else
    {
        mkdir(argv[1], 0);
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_mkdir, mkdir, Create the DIRECTORY.);

static int cmd_mkfs(int argc, char **argv)
{
    int result = 0;
    char *type = "elm"; /* use the default file system type as 'fatfs' */

    if (argc == 2)
    {
        result = dfs_mkfs(type, argv[1]);
    }
    else if (argc == 4)
    {
        if (strcmp(argv[1], "-t") == 0)
        {
            type = argv[2];
            result = dfs_mkfs(type, argv[3]);
        }
    }
    else
    {
        printf("Usage: mkfs [-t type] device\n");
        return 0;
    }

    if (result != RT_EOK)
    {
        printf("mkfs failed, result=%d\n", result);
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_mkfs, mkfs, format disk with file system);

static void _ls(const char *path)
{
    struct stat st;
    DIR *dir;
    struct dirent *dirent;

    /* list directory */
    dir = opendir(path);
    if (dir != 0)
    {
        printf("Directory %s:\n", path);
        do
        {
            dirent = readdir(dir);

            if (dirent != 0)
            {
                char* fn;
                int n;

                n = strlen(path) + dirent->d_namlen + 1;
                fn =  rt_malloc(n);
                memset(&stat, 0, sizeof(struct stat));
                snprintf(fn, n, "%s%s", path, dirent->d_name);

                if (stat(fn, &st) == 0)
                {
                    printf("%-20s", dirent->d_name);
                    if (S_ISDIR(st.st_mode))
                    {
                        printf("%-25s\n", "<DIR>");
                    }
                    else
                    {
                        printf("%-25lu\n", st.st_size);
                    }
                }
                else
                    printf("BAD file: %s\n", dirent->d_name);
                rt_free(fn);
            }
        }while(dirent != 0);

        closedir(dir);
    }
    else
    {
        printf("No such directory\n");
    }
}

static int cmd_ls(int argc, char **argv)
{
    if (argc == 1)
    {
        _ls("/");
    }
    else
    {
        _ls(argv[1]);
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_ls, ls, List information about the FILEs.);
