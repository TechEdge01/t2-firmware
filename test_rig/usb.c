#include "usb.h"
#include "test_rig.h"
#include "class/cdc/cdc_standard.h"

#define DIGITAL_CONTROL  1
#define ANALOG_SAMPLE    2
#define READ_ALL_DIGITAL 3

#define REQ_INFO 0x30
#define REQ_INFO_GIT_HASH 0x0
#define REQ_BOOT 0xBB

USB_ENDPOINTS(5);

__attribute__((__aligned__(4))) const USB_DeviceDescriptor device_descriptor = {
	.bLength = sizeof(USB_DeviceDescriptor),
	.bDescriptorType = USB_DTYPE_Device,

	.bcdUSB                 = 0x0200,
	.bDeviceClass           = 0,
	.bDeviceSubClass        = USB_CSCP_NoDeviceSubclass,
	.bDeviceProtocol        = USB_CSCP_NoDeviceProtocol,

	.bMaxPacketSize0        = 64,
	.idVendor               = 0x59E3,
	.idProduct              = 0xCDA6,
	.bcdDevice              = 0x0110,

	.iManufacturer          = 0x01,
	.iProduct               = 0x02,
	.iSerialNumber          = 0x03,

	.bNumConfigurations     = 1
};

uint16_t altsetting = 0;

typedef struct ConfigDesc {
	USB_ConfigurationDescriptor Config;
	USB_InterfaceDescriptor DAPInterfaceOff;
	USB_InterfaceDescriptor DAPInterface;
	USB_EndpointDescriptor DAPInEndpoint;
	USB_EndpointDescriptor DAPOutEndpoint;
	USB_InterfaceDescriptor EventInterface;
	USB_EndpointDescriptor ReportInEndpoint;
}  __attribute__((packed)) ConfigDesc;

__attribute__((__aligned__(4))) const ConfigDesc configuration_descriptor = {
	.Config = {
		.bLength = sizeof(USB_ConfigurationDescriptor),
		.bDescriptorType = USB_DTYPE_Configuration,
		.wTotalLength  = sizeof(ConfigDesc),
		.bNumInterfaces = 2,
		.bConfigurationValue = 1,
		.iConfiguration = 0,
		.bmAttributes = USB_CONFIG_ATTR_BUSPOWERED,
		.bMaxPower = USB_CONFIG_POWER_MA(500)
	},
	.DAPInterfaceOff = {
		.bLength = sizeof(USB_InterfaceDescriptor),
		.bDescriptorType = USB_DTYPE_Interface,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 0,
		.bInterfaceClass = USB_CSCP_VendorSpecificClass,
		.bInterfaceSubClass = 0x00,
		.bInterfaceProtocol = 0x00,
		.iInterface = 0x10,
	},
	.DAPInterface = {
		.bLength = sizeof(USB_InterfaceDescriptor),
		.bDescriptorType = USB_DTYPE_Interface,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 1,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_CSCP_VendorSpecificClass,
		.bInterfaceSubClass = 0x00,
		.bInterfaceProtocol = 0x00,
		.iInterface = 0x10,
	},
	.DAPInEndpoint = {
		.bLength = sizeof(USB_EndpointDescriptor),
		.bDescriptorType = USB_DTYPE_Endpoint,
		.bEndpointAddress = USB_EP_DAP_HID_IN,
		.bmAttributes = (USB_EP_TYPE_BULK | ENDPOINT_ATTR_NO_SYNC | ENDPOINT_USAGE_DATA),
		.wMaxPacketSize = 64,
		.bInterval = 0x00
	},
	.DAPOutEndpoint = {
		.bLength = sizeof(USB_EndpointDescriptor),
		.bDescriptorType = USB_DTYPE_Endpoint,
		.bEndpointAddress = USB_EP_DAP_HID_OUT,
		.bmAttributes = (USB_EP_TYPE_BULK | ENDPOINT_ATTR_NO_SYNC | ENDPOINT_USAGE_DATA),
		.wMaxPacketSize = 64,
		.bInterval = 0x00
	},
	.EventInterface = {
		.bLength = sizeof(USB_InterfaceDescriptor),
		.bDescriptorType = USB_DTYPE_Interface,
		.bInterfaceNumber = 1,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = USB_CSCP_VendorSpecificClass,
		.bInterfaceSubClass = 0x00,
		.bInterfaceProtocol = 0x00,
		.iInterface = 0x11,
	},
	.ReportInEndpoint = {
		.bLength = sizeof(USB_EndpointDescriptor),
		.bDescriptorType = USB_DTYPE_Endpoint,
		.bEndpointAddress = USB_EP_REPORT_IN,
		.bmAttributes = (USB_EP_TYPE_INTERRUPT | ENDPOINT_ATTR_NO_SYNC | ENDPOINT_USAGE_DATA),
		.wMaxPacketSize = 8,
		.bInterval = 10,
	},
};

__attribute__((__aligned__(4))) const USB_StringDescriptor language_string = {
	.bLength = USB_STRING_LEN(1),
	.bDescriptorType = USB_DTYPE_String,
	.bString = {USB_LANGUAGE_EN_US},
};

__attribute__((__aligned__(4))) const USB_StringDescriptor msft_os = {
	.bLength = 18,
	.bDescriptorType = USB_DTYPE_String,
	.bString = u"MSFT100\xee"
};

__attribute__((__aligned__(4))) const USB_MicrosoftCompatibleDescriptor msft_compatible = {
	.dwLength = sizeof(USB_MicrosoftCompatibleDescriptor) + sizeof(USB_MicrosoftCompatibleDescriptor_Interface),
	.bcdVersion = 0x0100,
	.wIndex = 0x0004,
	.bCount = 1,
	.reserved = {0, 0, 0, 0, 0, 0, 0},
	.interfaces = {
		{
			.bFirstInterfaceNumber = 0,
			.reserved1 = 0,
			.compatibleID = "WINUSB\0\0",
			.subCompatibleID = {0, 0, 0, 0, 0, 0, 0, 0},
			.reserved2 = {0, 0, 0, 0, 0, 0},
		}
	}
};

uint16_t usb_cb_get_descriptor(uint8_t type, uint8_t index, const uint8_t** ptr) {
	const void* address = NULL;
	uint16_t size    = 0;

	switch (type) {
		case USB_DTYPE_Device:
			address = &device_descriptor;
			size    = sizeof(USB_DeviceDescriptor);
			break;
		case USB_DTYPE_Configuration:
			address = &configuration_descriptor;
			size    = sizeof(ConfigDesc);
			break;
		case USB_DTYPE_String:
			switch (index) {
				case 0x00:
					address = &language_string;
					break;
				case 0x01:
					address = usb_string_to_descriptor("Technical Machine");
					break;
				case 0x02:
					address = usb_string_to_descriptor("Tessel 2 Test Rig");
					break;
				case 0x03:
					address = samd_serial_number_string_descriptor();
					break;
				case 0x10:
					address = usb_string_to_descriptor("CMSIS-DAP");
					break;
				case 0x11:
					address = usb_string_to_descriptor("Events");
					break;
				case 0xee:
					address = &msft_os;
					break;
			}
			size = (((USB_StringDescriptor*)address))->bLength;
			break;
	}

	*ptr = address;
	return size;
}

void usb_cb_reset(void) {
}

bool usb_cb_set_configuration(uint8_t config) {
	if (config <= 1) {
		button_init();
		return true;
	}
	return false;
}

void req_info(uint16_t wIndex) {
    const char* str = 0;
    switch (wIndex) {
        case REQ_INFO_GIT_HASH:
            str = git_version;
            break;
        default:
            return usb_ep0_stall();
    }
    uint16_t len = strlen(str);
    if (len > USB_EP0_SIZE) len = USB_EP0_SIZE;
    memcpy(ep0_buf_in, str, len);
    usb_ep0_out();
    return usb_ep0_in(len);
}

void req_boot() {
    wdt_reset(GCLK_32K);
    usb_ep0_out();
    return usb_ep0_in(0);
}

void usb_cb_control_setup(void) {
	uint8_t recipient = usb_setup.bmRequestType & USB_REQTYPE_RECIPIENT_MASK;
	if (recipient == USB_RECIPIENT_DEVICE) {
		switch(usb_setup.bRequest) {
			case 0xee:
				return usb_handle_msft_compatible(&msft_compatible);
			case REQ_INFO: return req_info(usb_setup.wIndex);
			case DIGITAL_CONTROL:
				return usb_control_req_digital(usb_setup.wIndex, usb_setup.wValue);
			case READ_ALL_DIGITAL:
				return usb_control_req_digital_read_all();
			case ANALOG_SAMPLE:
				return usb_control_req_analog_read(usb_setup.wIndex, usb_setup.wValue);
			case REQ_BOOT: return req_boot();
		}
	} else if (recipient == USB_RECIPIENT_INTERFACE) {
	}
	return usb_ep0_stall();
}

void usb_cb_control_in_completion(void) {
}

void usb_cb_control_out_completion(void) {
}

void usb_cb_completion(void) {
	if (usb_ep_pending(USB_EP_DAP_HID_OUT)) {
		dap_handle_usb_out_completion();
		usb_ep_handled(USB_EP_DAP_HID_OUT);
	}

	if (usb_ep_pending(USB_EP_DAP_HID_IN)) {
		dap_handle_usb_in_completion();
		usb_ep_handled(USB_EP_DAP_HID_IN);
	}
}

bool usb_cb_set_interface(uint16_t interface, uint16_t new_altsetting) {
	if (interface == 0) {
		if (new_altsetting == 0) {
			dap_disable();
		} else if (new_altsetting == 1){
			dap_enable();
		} else {
			return false;
		}
		return true;
	}
	return false;
}
