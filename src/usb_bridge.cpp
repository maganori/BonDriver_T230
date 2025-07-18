// SPDX-License-Identifier: GPL-2.0-only
/*
	usb_bridge.cpp

	Copyright (C) 2024 hendecarows
*/

#include "usb_bridge.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <thread>
#include <vector>

#include "plog/Log.h"


namespace t230
{

USBBridge::~USBBridge()
{
	PLOGV << __func__;

	close_device();
}

void USBBridge::open_device()
{
	PLOGV << __func__;

	if (is_open_)
	{
		return;
	}
	else
	{
		USBDevice::open_device();
		probe_device();
	}
}

void USBBridge::close_device()
{
	PLOGV << __func__;

	if (is_open_)
	{
		control_stream(false);
	}

	USBDevice::close_device();
}

void USBBridge::control_stream(bool is_start)
{
	PLOGV << __func__ << (is_start ? " start" : " stop");

	using namespace std::chrono_literals;
	std::vector<uint8_t> read_buf;

	if (is_start)
	{
		stop_stream();
		auto result = false;
		for (auto i = 0; i < RETRY_WAIT_STREAM; i++)
		{
			control_usb({ 0x37, 0, 0 }, read_buf);
			std::this_thread::sleep_for(20ms);
			control_usb({ 0x36, 3, 0 }, read_buf);
			result = wait_stream();
			PLOGD << "wait stream count = " << i << " result = " << result;
			if (result) { break; }
		}
		if (result)
		{
			start_stream();
		}
		else
		{
			const auto msg = "failed to transfer stream";
			PLOGE << msg;
			throw std::runtime_error(msg);
		}
	}
	else
	{
		stop_stream();
		control_usb({ 0x37, 0, 0 }, read_buf);
	}
}

void USBBridge::probe_device()
{
	PLOGV << __func__;

	using namespace std::chrono_literals;
	auto has_probe = false;

	for (auto i = 0; i < RETRY_GPIO; i++)
	{
		try
		{
			if (product_id() == USB_PID_T230A)
			{
				control_gpio(0x87, 0);
				std::this_thread::sleep_for(20ms);
				control_gpio(0x86, 1);
				control_gpio(0x80, 0);
				std::this_thread::sleep_for(100ms);
				control_gpio(0x80, 1);
				std::this_thread::sleep_for(50ms);
				has_probe = true;
				break;
			}
			else if (product_id() == USB_PID_T230C2)
			{
				control_gpio(0x04, 1);
				std::this_thread::sleep_for(20ms);
				control_gpio(0x83, 0);
				control_gpio(0xc0, 1);
				std::this_thread::sleep_for(100ms);
				control_gpio(0x83, 1);
				control_gpio(0xc0, 0);
				std::this_thread::sleep_for(50ms);
				has_probe = true;
				break;
			}
		}
		catch (const std::exception& ex)
		{
			PLOGE << "gpio access error " << ex.what();
			reset_usb();
		}
	}

	if (!has_probe)
	{
		auto msg = "gpio access error";
		PLOGE << msg;
		throw std::runtime_error(msg);
	}
}

void USBBridge::control_gpio(uint8_t port, uint8_t value)
{
	std::vector<uint8_t> write_buf{ 0x0e, port, value };
	std::vector<uint8_t> read_buf{ 0 };

	control_usb(write_buf, read_buf);
}

}
