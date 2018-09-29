#include <rtthread.h>
#include <rtdevice.h>

#include <board.h>
#include "flash_api.h"

#ifdef MTD_USING_NOR
static flash_t _flash;

#define W25Q16_SECTOR_SIZE    4096

static int _erase(rt_nor_t *nor, loff_t addr, size_t len)
{
	uint32_t s, l;

	s = addr - 0x8000000;

	for (l = 0; l < len;)
	{
		flash_erase_sector(&_flash, s);
		s += W25Q16_SECTOR_SIZE;
		l += W25Q16_SECTOR_SIZE;
	}
     
	return 0;
}   

static int _read(rt_nor_t *nor, loff_t addr, uint8_t *buf, size_t size)
{
    addr -= 0x8000000;
    flash_stream_write(&_flash, addr, size, buf);
    
    return size;
}

static int _write(rt_nor_t *nor, loff_t addr, const uint8_t *buf, size_t size)
{
    addr -= 0x8000000;
    flash_stream_write(&_flash, addr, size, buf);
	
    return size;
}

static rt_nor_t _nor;
static const struct nor_ops _ops =
{
    _erase,
    _read,
    _write
};

static const struct mtd_part _part[2] =
{
    {"nor0", 0x80F5000, 0xA000},
    {"nor1", 0x8100000, 0x100000},
};

int hw_mtdnor_init(void)
{
    rt_mtd_nor_init(&_nor, 4*1024);
    return rt_mtd_register(&_nor.parent, _part, 2);
}
INIT_DEVICE_EXPORT(hw_mtdnor_init);
#endif
