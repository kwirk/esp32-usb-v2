#include "descriptor.hpp"

#include "usb_msc.hpp"

// #if CONFIG_TINYUSB_MSC_ENABLED
#include "esp_vfs_fat.h"
#include "ffconf.h"
#include "ff.h"
#include "diskio.h"
static std::vector<esptinyusb::USBMSC *> _device;

namespace esptinyusb
{

	USBMSC::USBMSC()
	{
		_device.push_back(this);
		_callbacks = new USBMSCcallbacks();
	}

	USBMSC::~USBMSC()
	{
		printf("destructor\n\n");

		// if (_callbacks)
		// 	delete (_callbacks);
	}

	bool USBMSC::begin(uint8_t eps)
	{
		auto intf = addInterface(); // we need to create 2 interfaces, even if later descriptor is built all in one, and the one is just a dummy interface
		intf->claimInterface();

		intf->addEndpoint(eps);
		intf->addEndpoint(eps + 1);

		stringIndex = addString(CONFIG_TINYUSB_DESC_MSC_STRING, -1);

		uint8_t tmp[] = {TUD_MSC_DESCRIPTOR((uint8_t)intf->ifIdx, (uint8_t)stringIndex, intf->endpoints.at(0)->epId, (uint8_t)(0x80 | intf->endpoints.at(1)->epId), 64)}; // highspeed 512
		intf->setDesc(tmp, sizeof(tmp));

		return true;
	}

	bool USBMSC::end()
	{
		return true;
	}

	void USBMSC::_onInquiry(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
	{
		_callbacks->onInquiry(lun, vendor_id, product_id, product_rev);
	}

	bool USBMSC::_onReady(uint8_t lun)
	{
		return _callbacks->onReady(lun);
	}

	void USBMSC::_onCapacity(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
	{
		_callbacks->onCapacity(lun, block_count, block_size);
	}

	bool USBMSC::_onStop(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
	{
		return _callbacks->onStop(lun, power_condition, start, load_eject);
	}

	int32_t USBMSC::_onRead(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
	{
		return _callbacks->onRead(lun, lba, offset, buffer, bufsize);
	}

	int32_t USBMSC::_onWrite(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
	{
		return _callbacks->onWrite(lun, lba, offset, buffer, bufsize);
	}

}

TU_ATTR_WEAK void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
	for (auto d : _device)
	{
		if (d->luns() == lun)
		{
			d->_onInquiry(lun, vendor_id, product_id, product_rev);
		}
	}
}

TU_ATTR_WEAK bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
	for (auto d : _device)
	{
		if (d->luns() == lun)
		{
			return d->_onReady(lun);
		}
	}
	return false;
}

TU_ATTR_WEAK void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
	for (auto d : _device)
	{
		if (d->luns() == lun)
		{
			d->_onCapacity(lun, block_count, block_size);
		}
	}
}

TU_ATTR_WEAK bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
	for (auto d : _device)
	{
		if (d->luns() == lun)
		{
			return d->_onStop(lun, power_condition, start, load_eject);
		}
	}
	return false;
}

TU_ATTR_WEAK int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
	for (auto d : _device)
	{
		if (d->luns() == lun)
		{
			return d->_onRead(lun, lba, offset, buffer, bufsize);
		}
	}
	return -1;
}

TU_ATTR_WEAK int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
	for (auto d : _device)
	{
		if (d->luns() == lun)
		{
			return d->_onWrite(lun, lba, offset, buffer, bufsize);
		}
	}
	return -1;
}

TU_ATTR_WEAK int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{

	void const *response = NULL;
	int16_t resplen = 0;

	// most scsi handled is input
	bool in_xfer = true;

	switch (scsi_cmd[0])
	{
	case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
		// Host is about to read/write etc ... better not to disconnect disk
		resplen = 0;
		break;
	case 0x35:
		{
			if (disk_ioctl(0, CTRL_SYNC, NULL) != RES_OK)
			{
				printf("failed to sync\n");
				// return false;
			}
		}
		break;
	default:
		// Set Sense = Invalid Command Operation
		tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

		// negative means error -> tinyusb could stall and/or response with failed status
		resplen = -1;
		break;
	}

	// return resplen must not larger than bufsize
	if (resplen > bufsize)
		resplen = bufsize;

	if (response && (resplen > 0))
	{
		if (in_xfer)
		{
			memcpy(buffer, response, resplen);
		}
		else
		{
			// SCSI output
		}
	}

	return resplen;
}

// Support multi LUNs
TU_ATTR_WEAK uint8_t tud_msc_get_maxlun_cb(void)
{
	return 1;
}

// #endif