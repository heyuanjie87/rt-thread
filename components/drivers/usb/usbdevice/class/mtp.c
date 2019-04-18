/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-04-16     heyuanjie    first version
 * 
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtservice.h>
#include <rtdevice.h>
/* need macro RT_USING_POSIX */
#include <dfs_file.h>
#include <dfs_poll.h>

#define DBG_ENABLE
#define DBG_SECTION_NAME  "mtpUSB"
#define DBG_LEVEL         DBG_LOG
#include <rtdbg.h>

struct mtp_descriptor
{
#ifdef RT_USB_DEVICE_COMPOSITE
    struct uiad_descriptor iad_desc;
#endif
    struct uinterface_descriptor intf_desc;
    struct uendpoint_descriptor ep_out_desc;
    struct uendpoint_descriptor ep_in_desc;
    struct uendpoint_descriptor ep_intr_desc;
};
typedef struct mtp_descriptor *mtp_desc_t;

struct mtp_device
{
    struct rt_device parent;

    uep_t ep_out;
    uep_t ep_in;
    uep_t ep_intr;

    struct rt_wqueue wq;
    struct rt_wqueue rq;
    rt_list_t wcomp;
    rt_list_t rcomp;
};
typedef struct mtp_device *mtp_device_t;

static struct udevice_descriptor _dev_desc =
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
    0x0100,                     //bcdDevice;
    USB_STRING_MANU_INDEX,      //iManufacturer;
    USB_STRING_PRODUCT_INDEX,   //iProduct;
    USB_STRING_SERIAL_INDEX,    //iSerialNumber;
    USB_DYNAMIC,                //bNumConfigurations;
};

static struct usb_qualifier_descriptor _dev_qualifier =
{
    sizeof(_dev_qualifier),          //bLength
    USB_DESC_TYPE_DEVICEQUALIFIER,  //bDescriptorType
    0x0200,                         //bcdUSB
    0x00,                           //bDeviceClass
    0x00,                           //bDeviceSubClass
    0x00,                           //bDeviceProtocol
    64,                             //bMaxPacketSize0
    0x01,                           //bNumConfigurations
    0,
};

struct mtp_descriptor _mtp_desc = 
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
        0x03,                       //bNumEndpoints
        0xFF,                       //bInterfaceClass:Vendor;
        0xFF,                       //bInterfaceSubClass;
        0x00,                       //bInterfaceProtocol;
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
    /*endpoint descriptor*/
    {
        USB_DESC_LENGTH_ENDPOINT,
        USB_DESC_TYPE_ENDPOINT,
        USB_DYNAMIC | USB_DIR_IN,
        USB_EP_ATTR_INT,
        USB_DYNAMIC,
        50,
    }
};

const static char* _ustring[] =
{
    "Language",
    "rtthread",
    "mtp",
    "32021919830108",
    "Configuration",
    "MTP",
    USB_STRING_OS
};

static struct usb_os_function_comp_id_descriptor _comp_id_desc = 
{
    .bFirstInterfaceNumber = USB_DYNAMIC,
    .reserved1          = 0x01,
    .compatibleID       = {"MTP"},
    .subCompatibleID    = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .reserved2          = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static rt_err_t _ep_out_handler(ufunction_t func, uio_request_t req)
{
    mtp_device_t wd = (mtp_device_t)func->user_data;

    LOG_D("epout %d of %d", req->actual, req->size);

    if (req->actual <= 0)
    {
        rt_usbd_io_request(func->device, wd->ep_out, req);
    }
    else
    {
        rt_list_insert_after(&wd->rcomp, &req->list);
        rt_wqueue_wakeup(&(wd->rq), (void *)POLLIN);
    }

    return 0;
}

static rt_err_t _ep_in_handler(ufunction_t func, uio_request_t req)
{
    mtp_device_t wd = (mtp_device_t)func->user_data;

    LOG_D("epin %d of %d", req->actual, req->size);

    if (req->actual <= 0)
    {

    }
    else
    {
        rt_list_insert_after(&wd->wcomp, &req->list);
        rt_wqueue_wakeup(&(wd->wq), (void *)POLLOUT);
    }

    return 0;
}

static rt_err_t _ep_intr_handler(ufunction_t func, uio_request_t req)
{
    return 0;
}

static rt_err_t _interface_handler(ufunction_t func, ureq_t setup)
{
    LOG_D("setup: %x %x", setup->request_type, setup->bRequest);

    return 0;
}

static rt_err_t _function_enable(ufunction_t func)
{
    struct mtp_device *wd;
    struct uio_request *req;

    wd = func->user_data;

    req = rt_usbd_req_alloc(EP_MAXPACKET(wd->ep_in));
    rt_list_insert_after(&wd->wcomp, &req->list);

    req = rt_usbd_req_alloc(EP_MAXPACKET(wd->ep_out));
    rt_usbd_io_request(func->device, wd->ep_out, req);

    return 0;
}

static rt_err_t _function_disable(ufunction_t func)
{
    struct mtp_device *wd;

    RT_ASSERT(func != RT_NULL);
    wd = func->user_data;

    rt_wqueue_wakeup(&(wd->rq), (void *)POLLHUP);
    rt_wqueue_wakeup(&(wd->wq), (void *)POLLHUP);

    return 0;
}

static const struct ufunction_ops _uf_ops =
{
    _function_enable,
    _function_disable,
    RT_NULL,
};

/* file ops */
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
    struct mtp_device *wd;
    struct ufunction *f;
    size_t rsize, tsize;
    struct uio_request *req;

    wd = (struct mtp_device *)fd->data;
    f = (struct ufunction *)wd->parent.user_data;

    if (!f->enabled)
        return -ENODEV;

    while (rt_list_isempty(&wd->rcomp))
    {
        if (fd->flags & O_NONBLOCK)
            return -EAGAIN;

        rt_wqueue_wait(&wd->rq, 0, RT_WAITING_FOREVER);
        if (!f->enabled)
            return -ENODEV;
    }

    req = rt_list_first_entry(&wd->rcomp, struct uio_request, list);
    rsize = size > req->actual? req->actual : size;
    rt_memcpy(buf, req->buffer + req->pos, rsize);
    req->actual -= rsize;
    req->pos += rsize;
    if (req->actual == 0)
    {
        rt_list_remove(&req->list);
        rt_usbd_io_request(f->device, wd->ep_out, req);
    }

    return rsize;
}

static int _file_write(struct dfs_fd *fd, const void *buf, size_t size)
{
    struct mtp_device *wd;
    struct ufunction *f;
    struct uio_request *req;
    int wlen;

    wd = (struct mtp_device *)fd->data;
    f = (struct ufunction *)wd->parent.user_data;

    if (!f->enabled)
        return -ENODEV;

    while (rt_list_isempty(&wd->wcomp))
    {
        if (fd->flags & O_NONBLOCK)
            return -EAGAIN;

        rt_wqueue_wait(&wd->wq, 0, RT_WAITING_FOREVER);
        if (!f->enabled)
            return -ENODEV;
    }

    req = rt_list_first_entry(&wd->wcomp, struct uio_request, list);;
    rt_list_remove(&req->list);
    wlen = size > req->bufsz? req->bufsz : size;
    req->size = wlen;
    rt_memcpy(req->buffer, buf, wlen);

    rt_usbd_io_request(f->device, wd->ep_in, req);

    return wlen;
}

static int _file_poll(struct dfs_fd *fd, struct rt_pollreq *req)
{
    int mask = 0;
    struct mtp_device *wd;
    struct ufunction *f;

    wd = (struct mtp_device *)fd->data;
    f = (struct ufunction *)wd->parent.user_data;

    if (!f->enabled)
        return POLLHUP;
    if (!rt_list_isempty(&wd->rcomp))
        mask |= POLLIN;
    else
        rt_poll_add(&wd->rq, req);

    if (!rt_list_isempty(&wd->wcomp))
        mask |= POLLOUT;
    else
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
//

static int rt_usb_winusb_init(ufunction_t func)
{
    rt_err_t ret;
    mtp_device_t wd = (mtp_device_t)func->user_data;

    wd->parent.type = RT_Device_Class_Miscellaneous;

    wd->parent.user_data = func;
    ret = rt_device_register(&wd->parent, "mtp", RT_DEVICE_FLAG_RDWR);

    wd->parent.fops = &_fops;
    rt_wqueue_init(&wd->rq);
    rt_wqueue_init(&wd->wq);
    rt_list_init(&wd->rcomp);
    rt_list_init(&wd->wcomp);

    return ret;
}

static mtp_device_t mtp_device_new(ufunction_t func)
{
    mtp_device_t mtp_device;

    mtp_device = (mtp_device_t)rt_calloc(1, sizeof(struct mtp_device));
    func->user_data = mtp_device;

    return mtp_device;
}

static int mtp_device_ep_new(mtp_device_t dev, ualtsetting_t set)
{
    mtp_desc_t des;

    des = (mtp_desc_t)set->desc;
    dev->ep_out = rt_usbd_endpoint_new(&des->ep_out_desc, _ep_out_handler);
    dev->ep_in = rt_usbd_endpoint_new(&des->ep_in_desc, _ep_in_handler);
    dev->ep_intr = rt_usbd_endpoint_new(&des->ep_intr_desc, _ep_intr_handler);

    /* add the int out and int in endpoint to the alternate setting */
    rt_usbd_altsetting_add_endpoint(set, dev->ep_out);
    rt_usbd_altsetting_add_endpoint(set, dev->ep_in);
    rt_usbd_altsetting_add_endpoint(set, dev->ep_intr);

    return 0;
}

static void _winusb_descriptor_config(mtp_desc_t des, rt_uint8_t cintf_nr, rt_uint8_t device_is_hs)
{
#ifdef RT_USB_DEVICE_COMPOSITE
    des->iad_desc.bFirstInterface = cintf_nr;
#endif
    des->ep_out_desc.wMaxPacketSize = device_is_hs ? 512 : 64;
    des->ep_in_desc.wMaxPacketSize = device_is_hs ? 512 : 64;
    des->ep_intr_desc.wMaxPacketSize = 64;

    _comp_id_desc.bFirstInterfaceNumber = cintf_nr;
}

static ufunction_t rt_usbd_function_mtp_create(udevice_t device)
{
    ufunction_t func;
    uintf_t winusb_intf;
    ualtsetting_t winusb_setting;
    mtp_device_t mtp_device;

    /* parameter check */
    RT_ASSERT(device != RT_NULL);

    /* set usb device string description */
    rt_usbd_device_set_string(device, _ustring);

    /* create a cdc function */
    func = rt_usbd_function_new(device, &_dev_desc, &_uf_ops);
    rt_usbd_device_set_qualifier(device, &_dev_qualifier);

    mtp_device = mtp_device_new(func);

    /* create an interface object */
    winusb_intf = rt_usbd_interface_new(device, _interface_handler);

    /* create an alternate setting object */
    winusb_setting = rt_usbd_altsetting_new(sizeof(struct mtp_descriptor));

    /* config desc in alternate setting */
    rt_usbd_altsetting_config_descriptor(winusb_setting, &_mtp_desc, (rt_off_t) & ((mtp_desc_t)0)->intf_desc);

    /* configure the hid interface descriptor */
    _winusb_descriptor_config(winusb_setting->desc, winusb_intf->intf_num, device->dcd->device_is_hs);

    /* create endpoint */
    mtp_device_ep_new(mtp_device, winusb_setting);

    /* add the alternate setting to the interface, then set default setting */
    rt_usbd_interface_add_altsetting(winusb_intf, winusb_setting);
    rt_usbd_set_altsetting(winusb_intf, 0);

    /* add the interface to the mass storage function */
    rt_usbd_function_add_interface(func, winusb_intf);

    rt_usbd_os_comp_id_desc_add_os_func_comp_id_desc(device->os_comp_id_desc, &_comp_id_desc);

    rt_usb_winusb_init(func);

    return func;
}

static struct udclass mtp_class =
{
    .rt_usbd_function_create = rt_usbd_function_mtp_create
};

int rt_usbd_mtp_class_register(void)
{
    rt_usbd_class_register(&mtp_class);
    return 0;
}
INIT_PREV_EXPORT(rt_usbd_mtp_class_register);
