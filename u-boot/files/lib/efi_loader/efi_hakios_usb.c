// SPDX-License-Identifier: GPL-2.0+
/*
 * HakiOS USB device protocol on U-Boot's USB gadget stack.
 *
 * The EFI loader talks to this transport through one stable HakiOS interface. The implementation drives any U-Boot USB
 * device controller with gadget support. The caller hands over its descriptors once (StartEx), queues one transfer per endpoint at a time (Send) and polls for what
 * happened (HandleEvent). The HakiOS loader speaks fastboot to the host through it (xnuloader/loader.c, usb_start()),
 * so the loader can use the same fastboot recovery path across supported U-Boot boards.
 *
 * The function layout follows the public Qualcomm EFI USB device interface, but uses a distinct HakiOS GUID and U-Boot's
 * generic gadget API. One configuration, bulk endpoints and high speed are implemented. The control endpoint
 * is answered here from the caller's descriptors (as USB 2.0, without the caller's BOS: see hakios_usb_setup()); the caller
 * sees only its bulk transfers and the cable's state.
 *
 * U-Boot's gadget drivers run from polling (dm_usb_gadget_handle_interrupts()), as the loader expects: HandleEvent
 * polls the controller once and returns the oldest event, if there is one.
 */

#define LOG_CATEGORY LOGC_EFI

#include <dm.h>
#include <efi_loader.h>
#include <env.h>
#include <log.h>
#include <time.h>
#include <vsprintf.h>
#include <malloc.h>
#include <asm/unaligned.h>
#include <linux/sizes.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#define EFI_HAKIOS_USB_DEVICE_PROTOCOL_GUID \
	EFI_GUID(0x0ebd8a1f, 0x88b5, 0x4b1e, 0xa6, 0x59, 0x8d, 0x79, 0xf2, 0x94, 0x2b, 0x31)

/* what HandleEvent reports (EFIUsbDevice.h) */
enum {
	USB_DEVICE_EVENT_NO_EVENT,
	USB_DEVICE_EVENT_DEVICE_STATE_CHANGE,
	USB_DEVICE_EVENT_TRANSFER_NOTIFICATION,
	USB_DEVICE_EVENT_OEM_EVENT,
};

enum {
	USB_DEVICE_TRANSFER_STATUS_ACTIVE,
	USB_DEVICE_TRANSFER_STATUS_COMPLETE_OK,
	USB_DEVICE_TRANSFER_STATUS_CANCELLED,
	USB_DEVICE_TRANSFER_STATUS_COMPLETE_ERROR,
};

enum {
	USB_DEVICE_STATE_CONNECTED,
	USB_DEVICE_STATE_DISCONNECTED,
};

union efi_hakios_usb_event_data {
	u32 device_state;
	struct {
		u32 status;
		u8 endpoint_index;	/* the endpoint's address, direction bit included */
		u64 bytes_completed;
		void *data_buffer;
	} transfer;
	u64 raw[8];
};

struct efi_hakios_usb_descriptor_set {
	const void *device_descriptor;
	const void **descriptors;		/* one configuration, with its interfaces and endpoints */
	const void *ss_device_descriptor;
	const void **ss_descriptors;
	const void *device_qualifier;
	const void *bos;
	u8 string_count;
	const void **strings;
};

struct efi_hakios_usb_device_protocol {
	u64 revision;
	efi_status_t (EFIAPI *start)(void *descriptors);
	efi_status_t (EFIAPI *send)(u8 endpoint, u64 size, void *buffer);
	efi_status_t (EFIAPI *handle_event)(u32 *msg, u64 *payload_size, union efi_hakios_usb_event_data *payload);
	efi_status_t (EFIAPI *allocate_transfer_buffer)(u64 size, void **buffer);
	efi_status_t (EFIAPI *free_transfer_buffer)(void *buffer);
	efi_status_t (EFIAPI *stop)(void);
	efi_status_t (EFIAPI *abort_xfer)(u8 endpoint);
	efi_status_t (EFIAPI *set_endpoint_stall_state)(u8 endpoint, bool stall);
	efi_status_t (EFIAPI *start_ex)(struct efi_hakios_usb_descriptor_set *set);
};

#define MAX_EPS		4		/* bulk endpoints of the one interface */
#define MAX_EVENTS	32
#define MAX_BUFFERS	8
#define CONFIG_MAX	512		/* the configuration descriptor, endpoints rewritten for this controller */

struct hakios_usb_ep {
	u8 address;			/* the caller's: what it sends to and is told about */
	struct usb_ep *ep;
	struct usb_endpoint_descriptor desc;
	struct usb_request *req;
	bool busy;
	void *user;			/* the caller's buffer of the transfer in flight */
	u64 want;			/* and how much it asked for */
	void *bounce;			/* for a buffer that is not a transfer buffer of ours */
	size_t bounce_size;
};

struct hakios_usb_event {
	u32 msg;
	union efi_hakios_usb_event_data data;
};

static struct {
	struct efi_hakios_usb_descriptor_set set;
	struct udevice *udc;
	struct usb_gadget *gadget;
	struct usb_request *ep0_req;
	u8 config[CONFIG_MAX];
	u16 config_len;
	struct hakios_usb_ep eps[MAX_EPS];
	int neps;
	bool started, configured;
	bool debug;			/* hakios_usb_debug=1: what happens on the bus, on the console */
	struct hakios_usb_event events[MAX_EVENTS];
	unsigned int ev_head, ev_tail;
	struct { void *buf; size_t size; } buffers[MAX_BUFFERS];
} hakios_usb;

#define usb_dbg(fmt, ...) do { if (hakios_usb.debug) printf("hakios-usb: " fmt, ##__VA_ARGS__); } while (0)

/*
 * What the host does over fastboot, on the screen: every command but the getvar polls, each download and upload with
 * its size, progress and speed, and every FAIL. Marked [oosexgang]; hakios_status=0 turns it off.
 */
#define STATUS "[oosexgang] "

static struct {
	bool on;
	bool download;			/* the last command was download: its DATA starts a receive */
	u64 total, got, next;		/* the data phase in progress (total 0: none) */
	bool in;			/* ... to the host */
	ulong start;
} fbs;

static void fbs_begin(u64 total, bool in)
{
	fbs.total = total;
	fbs.got = 0;
	fbs.next = total / 4;
	fbs.in = in;
	fbs.start = get_timer(0);
	printf(STATUS "%s %llu KiB\n", in ? "sending" : "receiving", total >> 10);
}

static void fbs_data(u64 n)
{
	ulong ms;

	fbs.got += n;
	if (fbs.got < fbs.total) {
		if (fbs.got >= fbs.next && fbs.total >= SZ_1M) {
			printf(STATUS "  %llu%%\n", fbs.got * 100 / fbs.total);
			fbs.next += fbs.total / 4;
		}
		return;
	}
	ms = max(get_timer(fbs.start), 1UL);
	printf(STATUS "%s %llu KiB in %lu ms (%llu KiB/s)\n", fbs.in ? "sent" : "received", fbs.total >> 10, ms,
	       (fbs.total >> 10) * 1000 / ms);
	fbs.total = 0;
}

/* what the host sent: a command, or a part of a download */
static void fbs_out(const u8 *data, u64 n)
{
	char cmd[64];
	u64 len = min_t(u64, n, sizeof(cmd) - 1);

	if (!fbs.on || !n)
		return;
	if (fbs.total && !fbs.in) {
		fbs_data(n);
		return;
	}
	for (u64 i = 0; i < len; i++)
		cmd[i] = data[i] >= 0x20 && data[i] < 0x7f ? data[i] : '.';
	cmd[len] = 0;
	fbs.download = !strncmp(cmd, "download:", 9);
	if (strncmp(cmd, "getvar:", 7))
		printf(STATUS "host: %s\n", cmd);
}

/* what goes to the host, as it is sent: an upload's data, or an answer (DATA starts a data phase, FAIL is said) */
static void fbs_in(const u8 *data, u64 n)
{
	if (!fbs.on || !n)
		return;
	if (fbs.total && fbs.in) {
		fbs_data(n);
		return;
	}
	if (n >= 12 && !memcmp(data, "DATA", 4)) {
		char hex[9];

		memcpy(hex, data + 4, 8);
		hex[8] = 0;
		fbs_begin(simple_strtoull(hex, NULL, 16), !fbs.download);
		fbs.download = false;
	} else if (n >= 4 && !memcmp(data, "FAIL", 4)) {
		printf(STATUS "loader: %.*s\n", (int)min_t(u64, n, 60), data);
	}
}

static void hakios_usb_event(u32 msg, const union efi_hakios_usb_event_data *data)
{
	unsigned int next = (hakios_usb.ev_tail + 1) % MAX_EVENTS;

	if (next == hakios_usb.ev_head) {
		log_warning("hakios usb: event queue full, an event is lost\n");
		return;
	}
	usb_dbg("event %u (%08x)\n", msg, (u32)data->raw[0]);
	hakios_usb.events[hakios_usb.ev_tail].msg = msg;
	hakios_usb.events[hakios_usb.ev_tail].data = *data;
	hakios_usb.ev_tail = next;
}

static void hakios_usb_state(u32 state)
{
	union efi_hakios_usb_event_data data = { .device_state = state };

	hakios_usb_event(USB_DEVICE_EVENT_DEVICE_STATE_CHANGE, &data);
}

static struct hakios_usb_ep *hakios_usb_find(u8 address)
{
	for (int i = 0; i < hakios_usb.neps; i++)
		if (hakios_usb.eps[i].address == address)
			return &hakios_usb.eps[i];
	return NULL;
}

/* the transfer buffer of ours that @buf is, and how big it is; 0 if it is not one */
static size_t hakios_usb_buffer_size(const void *buf)
{
	for (int i = 0; i < MAX_BUFFERS; i++)
		if (hakios_usb.buffers[i].buf == buf)
			return hakios_usb.buffers[i].size;
	return 0;
}

static void hakios_usb_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct hakios_usb_ep *e = req->context;
	union efi_hakios_usb_event_data data = {};
	u64 got = req->actual;

	e->busy = false;
	usb_dbg("done %02x status %d actual %u of %u\n", e->address, req->status, req->actual, req->length);
	if (!(e->address & USB_DIR_IN)) {
		/* the controller is asked for whole packets; the caller gets no more than it asked for */
		if (got > e->want)
			got = e->want;
		if (req->buf != e->user && !req->status)
			memcpy(e->user, req->buf, got);
		if (!req->status)
			fbs_out(req->buf, got);
	}
	data.transfer.status = req->status == -ESHUTDOWN || req->status == -ECONNRESET ?
			       USB_DEVICE_TRANSFER_STATUS_CANCELLED :
			       req->status ? USB_DEVICE_TRANSFER_STATUS_COMPLETE_ERROR :
			       USB_DEVICE_TRANSFER_STATUS_COMPLETE_OK;
	data.transfer.endpoint_index = e->address;
	data.transfer.bytes_completed = got;
	data.transfer.data_buffer = e->user;
	hakios_usb_event(USB_DEVICE_EVENT_TRANSFER_NOTIFICATION, &data);
}

/* Copy the caller's configuration with the endpoints this controller gave it, and pick those endpoints. */
static int hakios_usb_take_config(void)
{
	const u8 *src = hakios_usb.set.descriptors[0];
	u16 total = get_unaligned_le16(src + 2);

	if (src[1] != USB_DT_CONFIG || total > CONFIG_MAX) {
		log_err("hakios usb: a configuration of %u bytes is not taken\n", total);
		return -EINVAL;
	}
	memcpy(hakios_usb.config, src, total);
	hakios_usb.config_len = total;
	hakios_usb.neps = 0;
	usb_ep_autoconfig_reset(hakios_usb.gadget);
	for (u16 off = 0; off + 2 <= total && hakios_usb.config[off]; off += hakios_usb.config[off]) {
		struct usb_endpoint_descriptor *d = (void *)&hakios_usb.config[off];
		struct hakios_usb_ep *e;

		if (d->bDescriptorType != USB_DT_ENDPOINT)
			continue;
		if (hakios_usb.neps == MAX_EPS)
			return -E2BIG;
		e = &hakios_usb.eps[hakios_usb.neps];
		memset(e, 0, sizeof(*e));
		e->address = d->bEndpointAddress;
		memcpy(&e->desc, d, USB_DT_ENDPOINT_SIZE);
		e->desc.bLength = USB_DT_ENDPOINT_SIZE;
		/*
		 * Only the direction: autoconfig ORs the number of the endpoint it picks into the address, and the
		 * caller's 0x01 with DWC3's ep2out made 0x03, an endpoint the controller does not have (seen on the phone).
		 */
		e->desc.bEndpointAddress &= USB_DIR_IN;
		e->ep = usb_ep_autoconfig(hakios_usb.gadget, &e->desc);
		if (!e->ep) {
			log_err("hakios usb: no endpoint for %02x\n", e->address);
			return -ENODEV;
		}
		/*
		 * and the caller's packet size: autoconfig writes a full-speed one (64 bytes for bulk) for gadgets that
		 * keep their high-speed descriptors apart. The controller took 64 while the host was told 512, and every
		 * packet longer than 64 bytes was refused (seen on the phone: commands went, downloads did not).
		 */
		e->desc.wMaxPacketSize = d->wMaxPacketSize;
		usb_dbg("endpoint %02x is %s (%02x, %u bytes)\n", e->address, e->ep->name, e->desc.bEndpointAddress,
		     le16_to_cpu(e->desc.wMaxPacketSize));
		/* what the host is told is the endpoint the controller really has */
		d->bEndpointAddress = e->desc.bEndpointAddress;
		e->ep->driver_data = e;
		e->req = usb_ep_alloc_request(e->ep, 0);
		if (!e->req)
			return -ENOMEM;
		e->req->context = e;
		e->req->complete = hakios_usb_complete;
		hakios_usb.neps++;
	}
	return 0;
}

static void hakios_usb_disable(void)
{
	for (int i = 0; i < hakios_usb.neps; i++) {
		if (hakios_usb.eps[i].busy)
			usb_ep_dequeue(hakios_usb.eps[i].ep, hakios_usb.eps[i].req);
		usb_ep_disable(hakios_usb.eps[i].ep);
		hakios_usb.eps[i].busy = false;
	}
	if (hakios_usb.configured) {
		hakios_usb.configured = false;
		if (fbs.on)
			printf(STATUS "usb: the host is gone\n");
		fbs.total = 0;
		hakios_usb_state(USB_DEVICE_STATE_DISCONNECTED);
	}
}

static int hakios_usb_set_config(u16 value)
{
	usb_dbg("set configuration %u\n", value);
	hakios_usb_disable();
	if (value == 0)
		return 0;
	if (value != hakios_usb.config[5])	/* bConfigurationValue */
		return -EINVAL;
	for (int i = 0; i < hakios_usb.neps; i++) {
		int ret = usb_ep_enable(hakios_usb.eps[i].ep, &hakios_usb.eps[i].desc);

		usb_dbg("enable %02x: %d\n", hakios_usb.eps[i].address, ret);
		if (ret) {
			log_err("hakios usb: endpoint %02x not enabled: %d\n", hakios_usb.eps[i].address, ret);
			return ret;
		}
	}
	hakios_usb.configured = true;
	if (fbs.on)
		printf(STATUS "usb: the host is connected\n");
	hakios_usb_state(USB_DEVICE_STATE_CONNECTED);
	return 0;
}

static void hakios_usb_ep0_complete(struct usb_ep *ep, struct usb_request *req)
{
}

static int hakios_usb_string(u8 index, void *buf)
{
	const u8 *s;

	if (index >= hakios_usb.set.string_count || !hakios_usb.set.strings[index])
		return -EINVAL;
	s = hakios_usb.set.strings[index];
	memcpy(buf, s, s[0]);
	return s[0];
}

static int hakios_usb_setup(struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{
	u16 w_length = le16_to_cpu(ctrl->wLength);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	struct usb_request *req = hakios_usb.ep0_req;
	u8 *buf = req->buf;
	int value = -EOPNOTSUPP;

	usb_dbg("setup %02x %02x %04x %04x %04x\n", ctrl->bRequestType, ctrl->bRequest, w_value,
	     le16_to_cpu(ctrl->wIndex), w_length);
	if ((ctrl->bRequestType & USB_TYPE_MASK) != USB_TYPE_STANDARD)
		return -EOPNOTSUPP;

	switch (ctrl->bRequest) {
	case USB_REQ_GET_DESCRIPTOR:
		switch (w_value >> 8) {
		case USB_DT_DEVICE:
			memcpy(buf, hakios_usb.set.device_descriptor, USB_DT_DEVICE_SIZE);
			buf[7] = gadget->ep0->maxpacket;	/* bMaxPacketSize0 */
			/*
			 * USB 2.0 without a BOS, as U-Boot's own gadgets are: the caller's 2.10 and BOS promise Link
			 * Power Management, which U-Boot's DWC3 does not do. Windows took the promise, put the idle
			 * link into L1, and the device was gone from the bus three seconds after it came (seen on
			 * the phone).
			 */
			put_unaligned_le16(0x0200, buf + 2);
			value = USB_DT_DEVICE_SIZE;
			break;
		case USB_DT_DEVICE_QUALIFIER:
			if (!hakios_usb.set.device_qualifier)
				break;
			memcpy(buf, hakios_usb.set.device_qualifier, sizeof(struct usb_qualifier_descriptor));
			value = sizeof(struct usb_qualifier_descriptor);
			break;
		case USB_DT_OTHER_SPEED_CONFIG:
		case USB_DT_CONFIG:
			memcpy(buf, hakios_usb.config, hakios_usb.config_len);
			buf[1] = w_value >> 8;
			value = hakios_usb.config_len;
			break;
		case USB_DT_STRING:
			value = hakios_usb_string(w_value & 0xff, buf);
			break;
		}
		break;
	case USB_REQ_SET_CONFIGURATION:
		value = hakios_usb_set_config(w_value);
		break;
	case USB_REQ_GET_CONFIGURATION:
		buf[0] = hakios_usb.configured ? hakios_usb.config[5] : 0;
		value = 1;
		break;
	case USB_REQ_SET_INTERFACE:
		value = w_value == 0 ? 0 : -EINVAL;
		break;
	case USB_REQ_GET_INTERFACE:
		buf[0] = 0;
		value = 1;
		break;
	}
	if (value < 0)
		return value;
	req->length = min_t(u16, w_length, value);
	req->zero = req->length < w_length;
	req->complete = hakios_usb_ep0_complete;
	return usb_ep_queue(gadget->ep0, req, 0);
}

static void hakios_usb_disconnect(struct usb_gadget *gadget)
{
	usb_dbg("disconnect or reset\n");
	hakios_usb_disable();
}

static int hakios_usb_bind(struct usb_gadget *gadget)
{
	hakios_usb.gadget = gadget;
	hakios_usb.ep0_req = usb_ep_alloc_request(gadget->ep0, 0);
	if (!hakios_usb.ep0_req)
		return -ENOMEM;
	hakios_usb.ep0_req->buf = memalign(ARCH_DMA_MINALIGN, CONFIG_MAX);
	if (!hakios_usb.ep0_req->buf)
		return -ENOMEM;
	return hakios_usb_take_config();
}

static void hakios_usb_unbind(struct usb_gadget *gadget)
{
	for (int i = 0; i < hakios_usb.neps; i++) {
		usb_ep_free_request(hakios_usb.eps[i].ep, hakios_usb.eps[i].req);
		free(hakios_usb.eps[i].bounce);
	}
	hakios_usb.neps = 0;
	free(hakios_usb.ep0_req->buf);
	usb_ep_free_request(gadget->ep0, hakios_usb.ep0_req);
	hakios_usb.ep0_req = NULL;
	hakios_usb.gadget = NULL;
}

static struct usb_gadget_driver hakios_usb_driver = {
	.function = "hakios-usb-device",
	.speed = USB_SPEED_HIGH,
	.bind = hakios_usb_bind,
	.unbind = hakios_usb_unbind,
	.setup = hakios_usb_setup,
	.disconnect = hakios_usb_disconnect,
	.reset = hakios_usb_disconnect,
};

static efi_status_t EFIAPI hakios_usb_start(void *descriptors)
{
	EFI_ENTRY("%p", descriptors);
	return EFI_EXIT(EFI_UNSUPPORTED);
}

static efi_status_t EFIAPI hakios_usb_start_ex(struct efi_hakios_usb_descriptor_set *set)
{
	int ret;

	EFI_ENTRY("%p", set);
	if (!set || !set->device_descriptor || !set->descriptors || !set->descriptors[0])
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	if (hakios_usb.started)
		return EFI_EXIT(EFI_ALREADY_STARTED);
	hakios_usb.set = *set;
	hakios_usb.debug = env_get_yesno("hakios_usb_debug") == 1;
	fbs.on = env_get_yesno("hakios_status") != 0;
	if (fbs.on)
		printf(STATUS "usb: up, waiting for the host\n");
	hakios_usb.ev_head = hakios_usb.ev_tail = 0;
	ret = udc_device_get_by_index(0, &hakios_usb.udc);
	if (ret) {
		log_err("hakios usb: no USB device controller: %d\n", ret);
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}
	ret = usb_gadget_register_driver(&hakios_usb_driver);
	if (ret) {
		log_err("hakios usb: the gadget did not start: %d\n", ret);
		udc_device_put(hakios_usb.udc);
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}
	hakios_usb.started = true;
	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI hakios_usb_stop(void)
{
	EFI_ENTRY("");
	if (!hakios_usb.started)
		return EFI_EXIT(EFI_NOT_STARTED);
	hakios_usb_disable();
	usb_gadget_unregister_driver(&hakios_usb_driver);
	udc_device_put(hakios_usb.udc);
	hakios_usb.started = false;
	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI hakios_usb_send(u8 endpoint, u64 size, void *buffer)
{
	struct hakios_usb_ep *e;
	struct usb_request *req;
	u64 length = size;
	size_t own;
	int ret;

	EFI_ENTRY("%02x, %llu, %p", endpoint, size, buffer);
	e = hakios_usb_find(endpoint);
	if (!e || !buffer)
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	if (!hakios_usb.configured)
		return EFI_EXIT(EFI_NOT_READY);
	if (e->busy)
		return EFI_EXIT(EFI_ALREADY_STARTED);
	/* a receive is of whole packets: the host's last one is short, or the length was a multiple anyway */
	if (!(endpoint & USB_DIR_IN))
		length = roundup(size, (u64)e->ep->maxpacket);
	req = e->req;
	own = hakios_usb_buffer_size(buffer);
	if (own >= length) {
		req->buf = buffer;
	} else {
		if (e->bounce_size < length) {
			free(e->bounce);
			e->bounce = memalign(ARCH_DMA_MINALIGN, length);
			e->bounce_size = e->bounce ? length : 0;
			if (!e->bounce)
				return EFI_EXIT(EFI_OUT_OF_RESOURCES);
		}
		req->buf = e->bounce;
		if (endpoint & USB_DIR_IN)
			memcpy(req->buf, buffer, size);
	}
	if (endpoint & USB_DIR_IN)
		fbs_in(buffer, size);
	req->length = length;
	req->zero = 0;
	e->user = buffer;
	e->want = size;
	e->busy = true;
	ret = usb_ep_queue(e->ep, req, 0);
	usb_dbg("send %02x %llu (%llu): %d\n", endpoint, size, length, ret);
	if (ret) {
		e->busy = false;
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}
	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI hakios_usb_handle_event(u32 *msg, u64 *payload_size, union efi_hakios_usb_event_data *payload)
{
	EFI_ENTRY("%p, %p, %p", msg, payload_size, payload);
	if (!msg || !payload_size || !payload)
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	if (!hakios_usb.started)
		return EFI_EXIT(EFI_NOT_STARTED);
	if (hakios_usb.ev_head == hakios_usb.ev_tail)
		dm_usb_gadget_handle_interrupts(hakios_usb.udc);
	if (hakios_usb.ev_head == hakios_usb.ev_tail) {
		*msg = USB_DEVICE_EVENT_NO_EVENT;
		*payload_size = 0;
		return EFI_EXIT(EFI_SUCCESS);
	}
	*msg = hakios_usb.events[hakios_usb.ev_head].msg;
	*payload = hakios_usb.events[hakios_usb.ev_head].data;
	*payload_size = sizeof(*payload);
	hakios_usb.ev_head = (hakios_usb.ev_head + 1) % MAX_EVENTS;
	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI hakios_usb_allocate_transfer_buffer(u64 size, void **buffer)
{
	EFI_ENTRY("%llu, %p", size, buffer);
	if (!buffer || !size)
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	for (int i = 0; i < MAX_BUFFERS; i++) {
		size_t room;

		if (hakios_usb.buffers[i].buf)
			continue;
		/* room for a receive rounded up to whole packets, so that the controller fills it directly */
		room = roundup(size, SZ_4K) + SZ_4K;
		hakios_usb.buffers[i].buf = memalign(SZ_4K, room);
		if (!hakios_usb.buffers[i].buf)
			return EFI_EXIT(EFI_OUT_OF_RESOURCES);
		hakios_usb.buffers[i].size = room;
		*buffer = hakios_usb.buffers[i].buf;
		return EFI_EXIT(EFI_SUCCESS);
	}
	return EFI_EXIT(EFI_OUT_OF_RESOURCES);
}

static efi_status_t EFIAPI hakios_usb_free_transfer_buffer(void *buffer)
{
	EFI_ENTRY("%p", buffer);
	for (int i = 0; i < MAX_BUFFERS; i++) {
		if (hakios_usb.buffers[i].buf == buffer) {
			free(buffer);
			hakios_usb.buffers[i].buf = NULL;
			hakios_usb.buffers[i].size = 0;
			return EFI_EXIT(EFI_SUCCESS);
		}
	}
	return EFI_EXIT(EFI_INVALID_PARAMETER);
}

static efi_status_t EFIAPI hakios_usb_abort_xfer(u8 endpoint)
{
	struct hakios_usb_ep *e;

	EFI_ENTRY("%02x", endpoint);
	e = hakios_usb_find(endpoint);
	if (!e)
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	if (e->busy)
		usb_ep_dequeue(e->ep, e->req);
	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI hakios_usb_set_endpoint_stall_state(u8 endpoint, bool stall)
{
	struct hakios_usb_ep *e;
	int ret;

	EFI_ENTRY("%02x, %d", endpoint, stall);
	e = hakios_usb_find(endpoint);
	if (!e)
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	ret = stall ? usb_ep_set_halt(e->ep) : usb_ep_clear_halt(e->ep);
	return EFI_EXIT(ret ? EFI_DEVICE_ERROR : EFI_SUCCESS);
}

static struct efi_hakios_usb_device_protocol hakios_usb_protocol = {
	.revision = 0x00010001,
	.start = hakios_usb_start,
	.send = hakios_usb_send,
	.handle_event = hakios_usb_handle_event,
	.allocate_transfer_buffer = hakios_usb_allocate_transfer_buffer,
	.free_transfer_buffer = hakios_usb_free_transfer_buffer,
	.stop = hakios_usb_stop,
	.abort_xfer = hakios_usb_abort_xfer,
	.set_endpoint_stall_state = hakios_usb_set_endpoint_stall_state,
	.start_ex = hakios_usb_start_ex,
};

static const efi_guid_t efi_hakios_usb_device_guid = EFI_HAKIOS_USB_DEVICE_PROTOCOL_GUID;

/**
 * efi_hakios_usb_register() - install the HakiOS USB device protocol
 *
 * Return: status code
 */
efi_status_t efi_hakios_usb_register(void)
{
	efi_handle_t handle = NULL;

	return efi_install_multiple_protocol_interfaces(&handle, &efi_hakios_usb_device_guid, &hakios_usb_protocol, NULL);
}
