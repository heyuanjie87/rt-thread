#ifndef __XIPFS_H__
#define __XIPFS_H__

#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>

#define XIPFS_DIRENT_NUM    32

#define XIPFS_DTYPE_FILE    1
#define XIPFS_DTYPE_DIR      2

struct xipfs_dev;

struct xipfs_dirent
{
    uint32_t magic;
    uint8_t type;
    uint8_t valid;    /* 0 deleted */
	uint8_t dless;   /* !0xFF data length less than expected */
    uint8_t rsv;
    char name[16];
    uint32_t size;
    uint32_t smask; /* size mask, clear a bit per 8KB */
    uint32_t data;    /* ralative address of data */
    uint16_t crc;
    uint16_t end;
};

struct xipfs_head
{
    uint32_t magic;
    uint32_t mvto;     /* 0 data move is completed */
};

struct xipfs
{
    struct xipfs_head hdr;
    struct xipfs_dirent ent[XIPFS_DIRENT_NUM];
    struct xipfs_dirent bak[XIPFS_DIRENT_NUM];
};

struct xipfs_nfops
{
    int (*erase)(struct xipfs_dev *d, uint32_t addr, uint32_t len);
    int (*write)(struct xipfs_dev *d, uint32_t addr, void *buf, uint32_t len);
    int (*mutex)(struct xipfs_dev *d, int op); /* op:1 lock 0 unlock */
};

struct xipfs_dev
{
    uint32_t start;                  /* start address of the xipfs(abs addr) */
    uint32_t blksize;                /* block size(min erase size) */
    uint32_t nblk;                   /* number of blocks */
    const struct xipfs_nfops *ops;
    void *lock;                      /* mutex lock(init by user) */

    uint32_t ta_size; /* total size */
    uint32_t av_size; /* available size */
    void *writer;
    void *ctx;
    struct xipfs_dirent root;
};

int xipfs_mount(struct xipfs_dev *dev);
int xipfs_open(struct xipfs_dev *dev, char *path, uint32_t flag, struct xipfs_dirent **ent);
int xipfs_close(struct xipfs_dev *dev, struct xipfs_dirent *d, uint32_t size);
int xipfs_read(struct xipfs_dev *dev, struct xipfs_dirent *d, uint32_t pos, uint8_t *buf, uint32_t size);
int xipfs_write(struct xipfs_dev *dev, struct xipfs_dirent *d, uint32_t pos, uint8_t *buf, uint32_t size);
int xipfs_mkfs(struct xipfs_dev *dev);
int xipfs_stat(struct xipfs_dev *dev, const char *path, struct stat *st);
int xipfs_getdirent(struct xipfs_dev *dev, struct xipfs_dirent *root, uint32_t pos, struct xipfs_dirent **ent);
int xipfs_mmap(struct xipfs_dev *dev, struct xipfs_dirent *d, uint32_t *addr);
int xipfs_unlink(struct xipfs_dev *dev, const char *path);

#endif
