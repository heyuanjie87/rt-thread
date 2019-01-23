#include <rtthread.h>
#include "drivers/usb_device.h"

int dcd_set_address(udcd_t dcd, rt_uint8_t address)
{
    RT_ASSERT(dcd != RT_NULL);
    RT_ASSERT(dcd->ops != RT_NULL);
    RT_ASSERT(dcd->ops->set_address != RT_NULL);

    return dcd->ops->set_address(dcd, address);
}

int dcd_ep_enable(udcd_t dcd, uep_t ep)
{
    RT_ASSERT(dcd != RT_NULL);
    RT_ASSERT(dcd->ops != RT_NULL);
    RT_ASSERT(dcd->ops->ep_enable != RT_NULL);

    return dcd->ops->ep_enable(dcd, ep);
}

int dcd_ep_disable(udcd_t dcd, uep_t ep)
{
    RT_ASSERT(dcd != RT_NULL);
    RT_ASSERT(dcd->ops != RT_NULL);
    RT_ASSERT(dcd->ops->ep_disable != RT_NULL);

    return dcd->ops->ep_disable(dcd, ep);
}

int dcd_ep_read_prepare(udcd_t dcd, rt_uint8_t address, void *buffer,
                               rt_size_t size)
{
    RT_ASSERT(dcd != RT_NULL);
    RT_ASSERT(dcd->ops != RT_NULL);

    if(dcd->ops->ep_read_prepare != RT_NULL)
    {
        return dcd->ops->ep_read_prepare(dcd, address, buffer, size);
    }
    else
    {
        return 0;
    }
}

int dcd_ep_read(udcd_t dcd, rt_uint8_t address, void *buffer)
{
    RT_ASSERT(dcd != RT_NULL);
    RT_ASSERT(dcd->ops != RT_NULL);

    if(dcd->ops->ep_read != RT_NULL)
    {
        return dcd->ops->ep_read(dcd, address, buffer);
    }
    else
    {
        return 0;
    }
}

int dcd_ep_write(udcd_t dcd, rt_uint8_t address, void *buffer,
                                 rt_size_t size)
{
    RT_ASSERT(dcd != RT_NULL);
    RT_ASSERT(dcd->ops != RT_NULL);
    RT_ASSERT(dcd->ops->ep_write != RT_NULL);

    return dcd->ops->ep_write(dcd, address, buffer, size);
}

int dcd_ep_set_stall(udcd_t dcd, rt_uint8_t address)
{    
    RT_ASSERT(dcd != RT_NULL);
    RT_ASSERT(dcd->ops != RT_NULL);
    RT_ASSERT(dcd->ops->ep_set_stall != RT_NULL);

    return dcd->ops->ep_set_stall(dcd, address);
}

int dcd_ep_clear_stall(udcd_t dcd, rt_uint8_t address)
{
    RT_ASSERT(dcd != RT_NULL);
    RT_ASSERT(dcd->ops != RT_NULL);
    RT_ASSERT(dcd->ops->ep_clear_stall != RT_NULL);

    return dcd->ops->ep_clear_stall(dcd, address);
}
