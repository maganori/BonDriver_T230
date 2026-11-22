// SPDX-License-Identifier: GPL-2.0-only
/*
	usb_device.h

	Copyright (C) 2024 hendecarows
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <winusb.h>

#include "i2c_buffer.h"
#include "usb_request_stream.h"
#include "ts_sync_queue.h"


namespace t230
{
class USBDevice
{
public:
	USBDevice() = default;
	virtual ~USBDevice();

	USBDevice(const USBDevice&) = delete;
	USBDevice& operator=(const USBDevice&) = delete;
	USBDevice(USBDevice&&) = delete;
	USBDevice& operator=(USBDevice&&) = delete;

	std::function<void(I2CBuffer&)> get_i2c_function();
	uint16_t vendor_id() const { return device_descriptor_.idVendor; }
	uint16_t product_id() const { return device_descriptor_.idProduct; }
	size_t stream_size() { return stream_queue_.stream_size(); }
	size_t queue_size() { return stream_queue_.size(); }

	void set_guid(const std::string& guid_string);
	void set_request_size(size_t size) { request_size_ = size; }
	void set_transfer_size(size_t size) { transfer_size_ = size; }
	void set_queue_size(size_t size) { queue_size_ = size; }
	virtual void open_device();
	virtual void close_device();
	bool get_stream(std::unique_ptr<std::vector<uint8_t>>& stream_buffer);
	// (patched) wake a thread blocked in get_stream()/SyncQueue::pop() so a
	// channel change can join the parser thread without waiting out the
	// 1000 ms pop timeout. The abort flag is reset by start_stream()'s clear().
	void abort_stream_queue() { stream_queue_.abort(); }

protected:
	static constexpr uint16_t USB_VID_T230 = 0x0572;
	static constexpr uint16_t USB_PID_T230A = 0x689a;
	static constexpr uint16_t USB_PID_T230C2 = 0xc68a;
	static constexpr uint8_t USB_ENDPOINT_READ = 0x81;
	static constexpr uint8_t USB_ENDPOINT_WRITE = 0x01;
	static constexpr uint8_t USB_ENDPOINT_STREAM = 0x82;
	static constexpr DWORD USB_TIMEOUT_READ = 2000;
	static constexpr DWORD USB_TIMEOUT_WRITE = 2000;
	static constexpr DWORD USB_TIMEOUT_WAIT_STREAM = 500;
	static constexpr DWORD USB_TIMEOUT_STREAM = 500;
	static constexpr bool USB_AUTO_SUSPEND = false;
	static constexpr bool USB_STREAM_RAW_IO = true;
	static constexpr size_t USB_WAIT_STREAM_SIZE = 8192;
	static constexpr size_t USB_STREAM_REQUEST_SIZE = 16;
	static constexpr size_t USB_STREAM_TRANSFER_SIZE = 8192;
	static constexpr size_t USB_STREAM_QUEUE_SIZE = 64;

	std::atomic_bool is_open_ = false;
	std::atomic_bool is_streaming_ = false;
	std::string device_guid_string_;
	GUID device_guid_ = { 0 };
	HANDLE device_handle_ = nullptr;
	WINUSB_INTERFACE_HANDLE winusb_handle_ = nullptr;
	HANDLE read_event_ = nullptr;
	HANDLE write_event_ = nullptr;
	USB_DEVICE_DESCRIPTOR device_descriptor_ = { 0 };
	std::mutex usb_mutex_;
	std::thread stream_thread_;
	USBRequestStream request_stream_;
	ts::SyncQueue stream_queue_;
	size_t request_size_ = USB_STREAM_REQUEST_SIZE;
	size_t transfer_size_ = USB_STREAM_TRANSFER_SIZE;
	size_t queue_size_ = USB_STREAM_QUEUE_SIZE;

	void open_usb_device();
	DWORD read_usb_bulk(std::vector<uint8_t>& read_buffer, size_t* read_size) const;
	DWORD write_usb_bulk(const std::vector<uint8_t>& write_buffer, size_t* write_size) const;
	void transfer_stream();
	void start_stream();
	void stop_stream();
	bool wait_stream() const;
	void control_usb(const std::vector<uint8_t>& write_buffer, std::vector<uint8_t>& read_buffer);
	void control_i2c(I2CBuffer& i2c_buffer);
	void reset_usb() const;
	bool has_vid_pid(uint16_t vid, uint16_t pid) const;
};
}
