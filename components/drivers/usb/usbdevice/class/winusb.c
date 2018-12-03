/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2017-11-16     ZYH          first version
 */
#include <rthw.h>
#include <rtthread.h>
#include <rtservice.h>
#include <rtdevice.h>
#include <drivers/usb_device.h>
#include "winusb.h"
#ifdef RT_USING_POSIX
#include <dfs_file.h>
#include <dfs_poll.h>
#endif

#ifndef WINUSB_MANUFAC_STRING
#define WINUSB_MANUFAC_STRING "Android"
#endif
#ifndef WINUSB_PRODUCT_STRING
#define WINUSB_PRODUCT_STRING "Android"
#endif
#ifndef WINUSB_INTERF_STRING
#define WINUSB_INTERF_STRING "ADB Interface"
#endif

struct winusb_device
{
    struct rt_device parent;
    void (*cmd_handler)(rt_uint8_t *buffer,rt_size_t size);
    rt_uint8_t cmd_buff[256];
    uep_t ep_out;
    uep_t ep_in;
#ifdef RT_USING_POSIX
    struct rt_wqueue wq;
    struct rt_wqueue rq;
    rt_uint16_t rdcnt;
    rt_uint16_t wrcnt;
#endif
};

typedef struct winusb_device * winusb_device_t;

ALIGN(4)
static struct udevice_descriptor dev_desc =
{
    USB_DESC_LENGTH_DEVICE,     //bLength;
    USB_DESC_TYPE_DEVICE,       //type;
    USB_BCD_VERSION,            //bcdUSB;
    0x00,                       //bDeviceClass;
    0x00,                       //bDeviceSubClass;
    0x00,                       //bDeviceProtocol;
    0x40,                       //bMaxPacketSize0;
    _VENDOR_ID,                 //idVendor;
    _PRODUCT_ID,                //idProduct;
    USB_BCD_DEVICE,             //bcdDevice;
    USB_STRING_MANU_INDEX,      //iManufacturer;
    USB_STRING_PRODUCT_INDEX,   //iProduct;
    USB_STRING_SERIAL_INDEX,    //iSerialNumber;
    USB_DYNAMIC,                //bNumConfigurations;
};

//FS and HS needed
ALIGN(4)
static struct usb_qualifier_descriptor dev_qualifier =
{
    sizeof(dev_qualifier),          //bLength
    USB_DESC_TYPE_DEVICEQUALIFIER,  //bDescriptorType
    0x0200,                         //bcdUSB
    0x00,                           //bDeviceClass
    0x00,                           //bDeviceSubClass
    0x00,                           //bDeviceProtocol
    64,                             //bMaxPacketSize0
    0x01,                           //bNumConfigurations
    0,
};

ALIGN(4)
struct winusb_descriptor _winusb_desc = 
{
#ifdef RT_USB_DEVICE_COMPOSITE
    /* Interface Association Descriptor */
    {
        USB_DESC_LENGTH_IAD,
        USB_DESC_TYPE_IAD,
        USB_DYNAMIC,
        0x01,
        0xFF,
        0x00,
        0x00,
        0x00,
    },
#endif
    /*interface descriptor*/
    {
        USB_DESC_LENGTH_INTERFACE,  //bLength;
        USB_DESC_TYPE_INTERFACE,    //type;
        USB_DYNAMIC,                //bInterfaceNumber;
        0x00,                       //bAlternateSetting;
        0x02,                       //bNumEndpoints
        0xFF,                       //bInterfaceClass;
        0x42,                       //bInterfaceSubClass;
        0x01,                       //bInterfaceProtocol;
        0x05,                       //iInterface;
    },
    /*endpoint descriptor*/
    {
        USB_DESC_LENGTH_ENDPOINT,
        USB_DESC_TYPE_ENDPOINT,
        USB_DYNAMIC | USB_DIR_OUT,
        USB_EP_ATTR_BULK,
        USB_DYNAMIC,
        0x00,
    },
    /*endpoint descriptor*/
    {
        USB_DESC_LENGTH_ENDPOINT,
        USB_DESC_TYPE_ENDPOINT,
        USB_DYNAMIC | USB_DIR_IN,
        USB_EP_ATTR_BULK,
        USB_DYNAMIC,
        0x00,
    },
};

ALIGN(4)
const static char* _ustring[] =
{
    "Language",
    WINUSB_MANUFAC_STRING,
    WINUSB_PRODUCT_STRING,
    "4985d80e9904",
    "Configuration",
    WINUSB_INTERF_STRING,
    USB_STRING_OS//must be
};

ALIGN(4)
struct usb_os_proerty winusb_proerty[] = 
{
    USB_OS_PROERTY_DESC(USB_OS_PROERTY_TYPE_REG_SZ,"DeviceInterfaceGUID",RT_WINUSB_GUID),
};

ALIGN(4)
struct usb_os_function_comp_id_descriptor winusb_func_comp_id_desc = 
{
    .bFirstInterfaceNumber = USB_DYNAMIC,
    .reserved1          = 0x01,
    .compatibleID       = {'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00},
    .subCompatibleID    = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .reserved2          = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static rt_err_t _ep_out_handler(ufunction_t func, rt_size_t size)
{
    winusb_device_t winusb_device = (winusb_device_t)func->user_data;
    if(winusb_device->parent.rx_indicate != RT_NULL)
    {
        winusb_device->parent.rx_indicate(&winusb_device->parent, size);
    }
#ifdef RT_USING_POSIX
    rt_wqueue_wakeup(&(winusb_device->rq), (void*)POLLIN);
    winusb_device->rdcnt ++;
#endif
    return RT_EOK;
}

static rt_err_t _ep_in_handler(ufunction_t func, rt_size_t size)
{
    winusb_device_t winusb_device = (winusb_device_t)func->user_data;
    if(winusb_device->parent.tx_complete != RT_NULL)
    {
        winusb_device->parent.tx_complete(&winusb_device->parent, winusb_device->ep_in->buffer);
    }
		rt_kprintf("in %d\n", size);
#ifdef RT_USING_POSIX
    rt_wqueue_wakeup(&(winusb_device->wq), (void*)POLLOUT);
#endif
    return RT_EOK;
}
static ufunction_t cmd_func = RT_NULL;
static rt_err_t _ep0_cmd_handler(udevice_t device, rt_size_t size)
{
    winusb_device_t winusb_device;
    
    if(cmd_func != RT_NULL)
    {
        winusb_device = (winusb_device_t)cmd_func->user_data;
        cmd_func = RT_NULL;
        if(winusb_device->cmd_handler != RT_NULL)
        {
            winusb_device->cmd_handler(winusb_device->cmd_buff,size);
        }
    }
    dcd_ep0_send_status(device->dcd);
    return RT_EOK;
}
static rt_err_t _ep0_cmd_read(ufunction_t func, ureq_t setup)
{
    winusb_device_t winusb_device = (winusb_device_t)func->user_data;
    cmd_func = func;
    rt_usbd_ep0_read(func->device,winusb_device->cmd_buff,setup->wLength,_ep0_cmd_handler);
    return RT_EOK;
}
static rt_err_t _interface_handler(ufunction_t func, ureq_t setup)
{
    switch(setup->bRequest)
    {
    case 'A':
        switch(setup->wIndex)
        {
        case 0x05:
            usbd_os_proerty_descriptor_send(func,setup,winusb_proerty,sizeof(winusb_proerty)/sizeof(winusb_proerty[0]));
            break;
        }
        break;
    case 0x0A://customer
        _ep0_cmd_read(func, setup);
        break;
    }
    
    return RT_EOK;
}

static int _ep_alloc_request(uep_t ep)
{
    int size;

    size = EP_MAXPACKET(ep);
    ep->buffer = rt_malloc(size);
    if (!ep->buffer)
        return -1;

    ep->request.buffer = ep->buffer;
    ep->request.size = size;
    ep->request.req_type = UIO_REQUEST_READ_BEST;

    return 0;
}

static rt_err_t _function_enable(ufunction_t func)
{
    struct winusb_device *wd;

    RT_ASSERT(func != RT_NULL);
    wd = func->user_data;

#ifdef RT_USING_POSIX
    wd->rdcnt = 0;
    wd->wrcnt = 0;
    rt_wqueue_init(&wd->rq);
    rt_wqueue_init(&wd->wq);
#endif

    _ep_alloc_request(wd->ep_out);
    _ep_alloc_request(wd->ep_in);

    rt_usbd_io_request(func->device, wd->ep_out, &wd->ep_out->request);

    return RT_EOK;
}
static rt_err_t _function_disable(ufunction_t func)
{
    RT_ASSERT(func != RT_NULL);
    return RT_EOK;
}

static struct ufunction_ops ops =
{
    _function_enable,
    _function_disable,
    RT_NULL,
};

static rt_err_t _winusb_descriptor_config(winusb_desc_t winusb, rt_uint8_t cintf_nr, rt_uint8_t device_is_hs)
{
#ifdef RT_USB_DEVICE_COMPOSITE
    winusb->iad_desc.bFirstInterface = cintf_nr;
#endif
    winusb->ep_out_desc.wMaxPacketSize = device_is_hs ? 512 : 64;
    winusb->ep_in_desc.wMaxPacketSize = device_is_hs ? 512 : 64;
    winusb_func_comp_id_desc.bFirstInterfaceNumber = cintf_nr;
    return RT_EOK;
}

static rt_size_t win_usb_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    if(((ufunction_t)dev->user_data)->device->state != USB_STATE_CONFIGURED)
    {
        return 0;
    }
    winusb_device_t winusb_device = (winusb_device_t)dev;
    winusb_device->ep_out->buffer = buffer;
    winusb_device->ep_out->request.buffer = buffer;
    winusb_device->ep_out->request.size = size;
    winusb_device->ep_out->request.req_type = UIO_REQUEST_READ_FULL;
    rt_usbd_io_request(((ufunction_t)dev->user_data)->device,winusb_device->ep_out,&winusb_device->ep_out->request);
    return size;
}
static rt_size_t win_usb_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    if(((ufunction_t)dev->user_data)->device->state != USB_STATE_CONFIGURED)
    {
        return 0;
    }
    winusb_device_t winusb_device = (winusb_device_t)dev;
    winusb_device->ep_in->buffer = (void *)buffer;
    winusb_device->ep_in->request.buffer = winusb_device->ep_in->buffer;
    winusb_device->ep_in->request.size = size;
    winusb_device->ep_in->request.req_type = UIO_REQUEST_WRITE;
    rt_usbd_io_request(((ufunction_t)dev->user_data)->device,winusb_device->ep_in,&winusb_device->ep_in->request);
    return size;
}
static rt_err_t  win_usb_control(rt_device_t dev, int cmd, void *args)
{
    winusb_device_t winusb_device = (winusb_device_t)dev;
    if(RT_DEVICE_CTRL_CONFIG == cmd)
    {
        winusb_device->cmd_handler = (void(*)(rt_uint8_t*,rt_size_t))args;
    }
    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
const static struct rt_device_ops winusb_device_ops =
{
    RT_NULL,
    RT_NULL,
    RT_NULL,
    win_usb_read,
    win_usb_write,
    win_usb_control,
};
#endif

#if 1//def RT_USING_POSIX
static int _file_open(struct dfs_fd *fd)
{
    return 0;
}

static int _file_close(struct dfs_fd *fd)
{
    return 0;
}

static int _file_ioctl(struct dfs_fd *fd, int cmd, void *args)
{
    return 0;
}

static int _file_read(struct dfs_fd *fd, void *buf, size_t size)
{
    struct winusb_device *wd;
    struct ufunction *f;
    struct uio_request *req;
    size_t rsize;

    wd = (struct winusb_device *)fd->data;
    f = (struct ufunction *)wd->parent.user_data;

    if (!f->enabled)
        return -1;

    while (!wd->rdcnt)
    {
        if (fd->flags & O_NONBLOCK)
			return -EAGAIN;

        rt_wqueue_wait(&wd->rq, 0, RT_WAITING_FOREVER);
    }

    req = &wd->ep_out->request;
    rsize = req->size - req->remain_size;
    if (rsize > size)
        rsize = size;
    rt_memcpy(buf, req->buffer, rsize);

    req->buffer = wd->ep_out->buffer;
    req->size = EP_MAXPACKET(wd->ep_out);
    req->req_type = UIO_REQUEST_READ_BEST;

    rt_usbd_io_request(f->device, wd->ep_out, req);

    return rsize;
}

static int _file_write(struct dfs_fd *fd, const void *buf, size_t size)
{
    struct winusb_device *wd;
    struct ufunction *f;

    wd = (struct winusb_device *)fd->data;
    f = (struct ufunction *)wd->parent.user_data;

    wd->ep_in->buffer = (void *)buf;
    wd->ep_in->request.buffer = wd->ep_in->buffer;
    wd->ep_in->request.size = size;
    wd->ep_in->request.req_type = UIO_REQUEST_WRITE;
    rt_usbd_io_request(f->device, wd->ep_in, &wd->ep_in->request);

    return 0;
}

static int _file_poll(struct dfs_fd *fd, struct rt_pollreq *req)
{
    int mask = 0;
    struct winusb_device *wd;
    struct ufunction *f;

    wd = (struct winusb_device *)fd->data;
    f = (struct ufunction *)wd->parent.user_data;

    rt_poll_add(&wd->wq, req);

    return mask;
}

static const struct dfs_file_ops _fops =
{
    _file_open,
    _file_close,
    _file_ioctl,
    _file_read,
    _file_write,
    RT_NULL,
    RT_NULL,
    RT_NULL,
    _file_poll
};
#endif

static rt_err_t rt_usb_winusb_init(ufunction_t func)
{
    rt_err_t ret;

    winusb_device_t winusb_device   = (winusb_device_t)func->user_data;
    winusb_device->parent.type      = RT_Device_Class_Miscellaneous;

#ifdef RT_USING_DEVICE_OPS
    winusb_device->parent.ops       = &winusb_device_ops;
#else
    winusb_device->parent.init      = RT_NULL;
    winusb_device->parent.open      = RT_NULL;
    winusb_device->parent.close     = RT_NULL;
    winusb_device->parent.read      = win_usb_read;
    winusb_device->parent.write     = win_usb_write;
    winusb_device->parent.control   = win_usb_control;
#endif

    winusb_device->parent.user_data = func;
    ret = rt_device_register(&winusb_device->parent, "winusb", RT_DEVICE_FLAG_RDWR);
#ifdef RT_USING_POSIX
    winusb_device->parent.fops = &_fops;
#endif

    return ret;
}

ufunction_t rt_usbd_function_winusb_create(udevice_t device)
{
    ufunction_t         func;
    winusb_device_t     winusb_device;

    uintf_t             winusb_intf;
    ualtsetting_t       winusb_setting;
    winusb_desc_t       winusb_desc;

    /* parameter check */
    RT_ASSERT(device != RT_NULL);

    /* set usb device string description */
    rt_usbd_device_set_string(device, _ustring);

    /* create a cdc function */
    func = rt_usbd_function_new(device, &dev_desc, &ops);
    rt_usbd_device_set_qualifier(device, &dev_qualifier);

    /* allocate memory for cdc vcom data */
    winusb_device = (winusb_device_t)rt_malloc(sizeof(struct winusb_device));
    rt_memset((void *)winusb_device, 0, sizeof(struct winusb_device));
    func->user_data = (void*)winusb_device;
    /* create an interface object */
    winusb_intf = rt_usbd_interface_new(device, _interface_handler);

    /* create an alternate setting object */
    winusb_setting = rt_usbd_altsetting_new(sizeof(struct winusb_descriptor));

    /* config desc in alternate setting */
    rt_usbd_altsetting_config_descriptor(winusb_setting, &_winusb_desc, (rt_off_t)&((winusb_desc_t)0)->intf_desc);

    /* configure the hid interface descriptor */
    _winusb_descriptor_config(winusb_setting->desc, winusb_intf->intf_num, device->dcd->device_is_hs);

    /* create endpoint */
    winusb_desc = (winusb_desc_t)winusb_setting->desc;
    winusb_device->ep_out = rt_usbd_endpoint_new(&winusb_desc->ep_out_desc, _ep_out_handler);
    winusb_device->ep_in  = rt_usbd_endpoint_new(&winusb_desc->ep_in_desc, _ep_in_handler);

    /* add the int out and int in endpoint to the alternate setting */
    rt_usbd_altsetting_add_endpoint(winusb_setting, winusb_device->ep_out);
    rt_usbd_altsetting_add_endpoint(winusb_setting, winusb_device->ep_in);

    /* add the alternate setting to the interface, then set default setting */
    rt_usbd_interface_add_altsetting(winusb_intf, winusb_setting);
    rt_usbd_set_altsetting(winusb_intf, 0);

    /* add the interface to the mass storage function */
    rt_usbd_function_add_interface(func, winusb_intf);

    rt_usbd_os_comp_id_desc_add_os_func_comp_id_desc(device->os_comp_id_desc, &winusb_func_comp_id_desc);
    /* initilize winusb */
    rt_usb_winusb_init(func);
    return func;
}

struct udclass winusb_class = 
{
    .rt_usbd_function_create = rt_usbd_function_winusb_create
};

int rt_usbd_winusb_class_register(void)
{
    rt_usbd_class_register(&winusb_class);
    return 0;
}
INIT_PREV_EXPORT(rt_usbd_winusb_class_register);
