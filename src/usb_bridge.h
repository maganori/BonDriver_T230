// SPDX-License-Identifier: GPL-2.0-only
/*
	usb_bridge.h

	Copyright (C) 2024 hendecarows
*/

#pragma once

#include <cstddef>
#include <cstdint>

#include "usb_device.h"


namespace t230
{
class USBBridge : public USBDevice
{
public:
	USBBridge() = default;
	virtual ~USBBridge();

	USBBridge(const USBBridge&) = delete;
	USBBridge& operator=(const USBBridge&) = delete;
	USBBridge(USBBridge&&) = delete;
	USBBridge& operator=(USBBridge&&) = delete;

	virtual void open_device() override;
	virtual void close_device() override;
	void control_stream(bool is_start);

private:
	static constexpr int32_t RETRY_GPIO = 2;
	static constexpr int32_t RETRY_WAIT_STREAM = 5;

	void probe_device();
	void control_gpio(uint8_t port, uint8_t value);
};
}
