// SPDX-License-Identifier: GPL-2.0-only
/*
	usb_device.cpp

	Copyright (C) 2024 hendecarows
*/

#include "usb_device.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <setupapi.h>

#include "plog/Log.h"

#include "i2c_buffer.h"
#include "utility.h"

namespace t230
{

USBDevice::~USBDevice()
{
	PLOGV << __func__;

	close_device();
}

std::function<void(I2CBuffer&)> USBDevice::get_i2c_function()
{
	return [this](I2CBuffer& i2c_buffer) { control_i2c(i2c_buffer); };
}

void USBDevice::set_guid(const std::string& guid_string)
{
	PLOGD << "GUID = " << guid_string;

	device_guid_string_ = guid_string;
	device_guid_ = utility::parse_guid(guid_string);
}

void USBDevice::open_device()
{
	PLOGV << __func__;
	PLOGD << "request_size = " << request_size_;
	PLOGD << "transfer_size = " << transfer_size_;
	PLOGD << "queue_size = " << queue_size_;

	if (is_open_) { return; }
	request_stream_.resize(request_size_, transfer_size_);
	stream_queue_.set_ts_queue_size(queue_size_);
	open_usb_device();
	is_open_ = true;
}

void USBDevice::close_device()
{
	PLOGV << __func__;

	if (read_event_)
	{
		CloseHandle(read_event_);
		read_event_ = nullptr;
	}

	if (write_event_)
	{
		CloseHandle(write_event_);
		write_event_ = nullptr;
	}

	if (winusb_handle_)
	{
		WinUsb_Free(winusb_handle_);
		winusb_handle_ = nullptr;
	}

	if (device_handle_)
	{
		CloseHandle(device_handle_);
		device_handle_ = nullptr;
	}

	if (is_open_)
	{
		request_stream_.clear();
		is_open_ = false;
	}
}

bool USBDevice::get_stream(std::unique_ptr<std::vector<uint8_t>>& stream_buffer)
{
	return stream_queue_.pop(stream_buffer);
}

void USBDevice::open_usb_device()
{
	PLOGV << __func__;

	PLOGV << "SetupDiGetClassDevs";
	auto dev_info = SetupDiGetClassDevs(&device_guid_, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE)
	{
		auto msg = std::format(
			"SetupDiGetClassDevs() error = {:d}",
			GetLastError());
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	HANDLE dev_handle = nullptr;
	SP_DEVICE_INTERFACE_DATA dev_iface = { 0 };
	std::vector<uint8_t> buf;

	DWORD i = 0;
	auto is_open = false;
	dev_iface.cbSize = sizeof(dev_iface);

	PLOGV << "SetupDiEnumDeviceInterfaces";
	while (SetupDiEnumDeviceInterfaces(dev_info, nullptr, &device_guid_, i++, &dev_iface))
	{
		DWORD dev_detail_size = 0;

		SetupDiGetDeviceInterfaceDetail(dev_info, &dev_iface, nullptr, 0, &dev_detail_size, nullptr);
		if (!dev_detail_size)
		{
			continue;
		}

		buf.resize(dev_detail_size);
		auto* dev_detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(buf.data());
		dev_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

		if (!SetupDiGetDeviceInterfaceDetail(dev_info, &dev_iface, dev_detail, dev_detail_size, nullptr, nullptr))
		{
			PLOGD << "SetupDiGetDeviceInterfaceDetail GetLastError = " << GetLastError() << " continue";
			continue;
		}

		dev_handle = CreateFile(dev_detail->DevicePath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (dev_handle == INVALID_HANDLE_VALUE)
		{
			PLOGD << "CreateFile() GetLastError = " << GetLastError() << " continue";
			continue;
		}

		is_open = true;
		break;
	}

	PLOGV << "SetupDiDestroyDeviceInfoList";
	SetupDiDestroyDeviceInfoList(dev_info);

	if (!is_open)
	{
		auto msg = std::format(
			"usb device open error guid = {}",
			device_guid_string_);
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	PLOGV << "WinUsb_Initialize";
	WINUSB_INTERFACE_HANDLE winusb_handle = nullptr;
	if (!WinUsb_Initialize(dev_handle, &winusb_handle))
	{
		CloseHandle(dev_handle);
		auto msg = std::format(
			"WinUsb_Initialize() error = {:d}",
			GetLastError());
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	PLOGV << "WinUsb_GetDescriptor";
	ULONG size = 0;
	USB_DEVICE_DESCRIPTOR dev_desc = { 0 };
	if (!WinUsb_GetDescriptor(winusb_handle, USB_DEVICE_DESCRIPTOR_TYPE, 0x00, 0x0000, reinterpret_cast<PUCHAR>(&dev_desc), sizeof(dev_desc), &size))
	{
		WinUsb_Free(winusb_handle);
		CloseHandle(dev_handle);
		auto msg = std::format(
			"WinUsb_GetDescriptor error = {:d}",
			GetLastError());
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	PLOGD << std::format("usb vid = {:#06x} pid = {:#06x}", dev_desc.idVendor, dev_desc.idProduct);
	if (!has_vid_pid(dev_desc.idVendor, dev_desc.idProduct))
	{
		auto msg = std::format(
			"unsupported usb device error vid = {:#06x} pid = {:#06x}",
			dev_desc.idVendor, dev_desc.idProduct);
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	device_handle_ = dev_handle;
	winusb_handle_ = winusb_handle;
	device_descriptor_ = dev_desc;
	read_event_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	write_event_ = CreateEvent(nullptr, TRUE, FALSE, nullptr);

	if (!read_event_ || !write_event_)
	{
		auto msg = "CreateEvent() error read or write";
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	PLOGV << "WinUsb_SetPipePolicy read endpoint";
	auto value = USB_TIMEOUT_READ;
	if (!WinUsb_SetPipePolicy(winusb_handle, USB_ENDPOINT_READ, PIPE_TRANSFER_TIMEOUT, sizeof(value), (LPVOID)&value))
	{
		auto msg = std::format(
			"WinUsb_SetPipePolicy() error for read endpoint GetLastError = {:d}",
			GetLastError());
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	PLOGV << "WinUsb_SetPipePolicy write endpoint";
	value = USB_TIMEOUT_WRITE;
	if (!WinUsb_SetPipePolicy(winusb_handle, USB_ENDPOINT_WRITE, PIPE_TRANSFER_TIMEOUT, sizeof(value), (LPVOID)&value))
	{
		auto msg = std::format(
			"WinUsb_SetPipePolicy() error for write endpoint GetLastError = {:d}",
			GetLastError());
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	PLOGV << "WinUsb_SetPowerPolicy write endpoint";
	value = USB_AUTO_SUSPEND;
	if (!WinUsb_SetPowerPolicy(winusb_handle, AUTO_SUSPEND, sizeof(value), (LPVOID)&value))
	{
		auto msg = std::format(
			"WinUsb_SetPowerPolicy() error for disable AUTO_SUSPEND GetLastError = {:d}",
			GetLastError());
		PLOGE << msg;
		throw std::runtime_error(msg);
	}
}

DWORD USBDevice::read_usb_bulk(std::vector<uint8_t>& read_buffer, size_t* read_size) const
{
	if (read_buffer.size() == 0) { return ERROR_BAD_ARGUMENTS; }

	DWORD result = ERROR_SUCCESS;
	ULONG transfer_size = 0;
	OVERLAPPED overlapped = { 0 };

	overlapped.hEvent = read_event_;
	ResetEvent(overlapped.hEvent);

	auto ret = WinUsb_ReadPipe(winusb_handle_, USB_ENDPOINT_READ, read_buffer.data(), static_cast<ULONG>(read_buffer.size()), nullptr, &overlapped);
	if (!ret)
	{
		result = GetLastError();
		if (result == ERROR_IO_PENDING)
		{
			WaitForSingleObject(overlapped.hEvent, USB_TIMEOUT_READ);
			ret = TRUE;
		}
	}

	if (ret)
	{
		ret = WinUsb_GetOverlappedResult(winusb_handle_, &overlapped, &transfer_size, FALSE);
		if (ret)
		{
			result = ERROR_SUCCESS;
		}
		else
		{
			result = GetLastError();
			transfer_size = 0;
		}
	}

	if (ret)
	{
		PLOGV << std::format(" BR {:02x}: {}", USB_ENDPOINT_READ, utility::hex_dump(read_buffer));
	}
	else
	{
		WinUsb_AbortPipe(winusb_handle_, USB_ENDPOINT_READ);
	}

	if (read_size) { *read_size = transfer_size; }
	return result;
}

DWORD USBDevice::write_usb_bulk(const std::vector<uint8_t>& write_buffer, size_t* write_size) const
{
	if (write_buffer.size() == 0) { return ERROR_BAD_ARGUMENTS; }

	PLOGV << std::format("BW {:02x}: {}", USB_ENDPOINT_WRITE, utility::hex_dump(write_buffer));

	DWORD result = ERROR_SUCCESS;
	ULONG transfer_size = 0;
	OVERLAPPED overlapped = { 0 };

	overlapped.hEvent = write_event_;
	ResetEvent(overlapped.hEvent);

	auto ret = WinUsb_WritePipe(winusb_handle_, USB_ENDPOINT_WRITE, const_cast<uint8_t*>(write_buffer.data()), static_cast<ULONG>(write_buffer.size()), nullptr, &overlapped);
	if (!ret)
	{
		result = GetLastError();
		if (result == ERROR_IO_PENDING)
		{
			WaitForSingleObject(overlapped.hEvent, USB_TIMEOUT_WRITE);
			ret = TRUE;
		}
	}

	if (ret)
	{
		ret = WinUsb_GetOverlappedResult(winusb_handle_, &overlapped, &transfer_size, FALSE);
		if (ret)
		{
			result = ERROR_SUCCESS;
		}
		else
		{
			result = GetLastError();
			transfer_size = 0;
		}
	}

	if (!ret)
	{
		WinUsb_AbortPipe(winusb_handle_, USB_ENDPOINT_WRITE);
	}

	if (write_size) { *write_size = transfer_size; }
	return result;
}

void USBDevice::transfer_stream()
{
	PLOGV << __func__;

	WinUsb_ResetPipe(winusb_handle_, USB_ENDPOINT_STREAM);

	try
	{
		UCHAR raw_io = USB_STREAM_RAW_IO ? 1 : 0;
		auto ret = WinUsb_SetPipePolicy(winusb_handle_, USB_ENDPOINT_STREAM, RAW_IO, sizeof(raw_io), &raw_io);
		if (!ret)
		{
			auto msg = std::format(
				"WinUsb_SetPipePolicy error for stream endpoint GetLastError = {:d}",
				GetLastError());
			PLOGE << msg;
			throw std::runtime_error(msg);
		}

		request_stream_.reset();
		for (auto& it : request_stream_.items())
		{
			auto ret = WinUsb_ReadPipe(winusb_handle_, USB_ENDPOINT_STREAM, it.buffer().data(), static_cast<ULONG>(it.buffer().size()), nullptr, &it.overlapped());

			if (ret || GetLastError() == ERROR_IO_PENDING)
			{
				it.is_waiting() = true;
			}
			else
			{
				it.is_waiting() = false;
				PLOGE << "WinUsb_ReadPipe error = " << GetLastError();
			}
		}

		while (is_streaming_)
		{
			auto& request = request_stream_.current();
			if (request.is_waiting())
			{
				auto result = WaitForSingleObject(request.handle_event(), USB_TIMEOUT_STREAM);
				if (result == WAIT_FAILED)
				{
					auto msg = std::format(
						"WaitForSingleObject return WAIT_FAILED GetLastError = {:d} and stop transfer",
						GetLastError());
					PLOGE << msg;
					throw std::runtime_error(msg);
				}
				else if (result == WAIT_TIMEOUT)
				{
					PLOGW << "WaitForSingleObject return WAIT_TIMEOUT GetLastError = "
						<< GetLastError();
					continue;
				}
				else
				{
					DWORD transfer_size = 0;
					auto ret = WinUsb_GetOverlappedResult(winusb_handle_, &request.overlapped(), &transfer_size, TRUE);
					if (ret)
					{
						if (transfer_size > 0)
						{
							request.buffer().resize(transfer_size);
							if (!stream_queue_.push(request.buffer()))
							{
								PLOGW << "failed to push stream into queue and lost size = "
									<< transfer_size;
							}
						}
						else
						{
							PLOGD << "ignore transfer stream size = " << transfer_size;
						}
					}
					else
					{
						PLOGW << "WinUsb_GetOverlappedResult return FALSE GetLastError = "
							<< GetLastError();
					}
				}
			}

			request.reset(request_stream_.buffer_size());
			auto ret = WinUsb_ReadPipe(winusb_handle_, USB_ENDPOINT_STREAM,
				request.buffer().data(), static_cast<ULONG>(request.buffer().size()), nullptr, &request.overlapped());

			if (ret || GetLastError() == ERROR_IO_PENDING)
			{
				request.is_waiting() = true;
			}
			else
			{
				request.is_waiting() = false;
				PLOGE << "WinUsb_ReadPipe return FALSE GetLastError = " << GetLastError();
			}
			request_stream_.move_next();
		}
	}
	catch (const std::exception& ex)
	{
		PLOGE << ex.what();
	}

	WinUsb_AbortPipe(winusb_handle_, USB_ENDPOINT_STREAM);
	UCHAR raw_io = 0;
	WinUsb_SetPipePolicy(winusb_handle_, USB_ENDPOINT_STREAM, RAW_IO, sizeof(raw_io), &raw_io);
}

void USBDevice::start_stream()
{
	PLOGV << __func__;

	stream_queue_.clear();
	is_streaming_ = true;
	stream_thread_ = std::thread(&USBDevice::transfer_stream, this);
	auto code = SetThreadPriority(stream_thread_.native_handle(), THREAD_PRIORITY_HIGHEST);
	if (!code)
	{
		PLOGE << "SetThreadPriority return FALSE GetLastError = " << GetLastError();
	}
}

void USBDevice::stop_stream()
{
	PLOGV << __func__;

	PLOGD << "is_streaming = " << is_streaming_;
	if (is_streaming_)
	{
		is_streaming_ = false;
		request_stream_.set();
		if (stream_thread_.joinable()) { stream_thread_.join(); }
	}
}

bool USBDevice::wait_stream() const
{
	PLOGV << __func__;

	DWORD result = ERROR_SUCCESS;
	ULONG transfer_size = 0;
	OVERLAPPED overlapped = { 0 };
	std::vector<UCHAR> buffer(USB_WAIT_STREAM_SIZE);

	overlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!overlapped.hEvent) { return false; }
	ResetEvent(overlapped.hEvent);

	auto ret = WinUsb_ReadPipe(winusb_handle_, USB_ENDPOINT_STREAM, buffer.data(), static_cast<ULONG>(buffer.size()), nullptr, &overlapped);
	if (!ret)
	{
		auto result = GetLastError();
		PLOGD << "WinUsb_ReadPipe GetLastError = " << result;
		if (result == ERROR_IO_PENDING)
		{
			WaitForSingleObject(overlapped.hEvent, USB_TIMEOUT_WAIT_STREAM);
			ret = TRUE;
		}
	}

	if (ret)
	{
		ret = WinUsb_GetOverlappedResult(winusb_handle_, &overlapped, &transfer_size, FALSE);
		if (ret)
		{
			result = ERROR_SUCCESS;
			PLOGD << "wait stream size = " << transfer_size;
		}
		else
		{
			result = GetLastError();
			transfer_size = 0;
		}
	}

	if (!ret)
	{
		WinUsb_AbortPipe(winusb_handle_, USB_ENDPOINT_STREAM);
	}

	CloseHandle(overlapped.hEvent);

	if (result) { return false; }

	return true;
}

void USBDevice::control_usb(const std::vector<uint8_t>& write_buffer, std::vector<uint8_t>& read_buffer)
{
	PLOGV << __func__;
	std::lock_guard<std::mutex> lock(usb_mutex_);

	if (write_buffer.size() > 0)
	{
		size_t write_size = 0;
		auto result = write_usb_bulk(write_buffer, &write_size);
		if (result != ERROR_SUCCESS || write_size != write_buffer.size())
		{
			auto msg = std::format(
				"usb bulk write error result = {:d} data = {} write size = {:d}",
				result, utility::hex_dump(write_buffer), write_size);
			PLOGE << msg;
			throw std::runtime_error(msg);
		}
	}

	if (read_buffer.size() > 0)
	{
		size_t read_size = 0;
		auto result = read_usb_bulk(read_buffer, &read_size);
		if (result != ERROR_SUCCESS || read_size != read_buffer.size())
		{
			auto msg = std::format(
				"usb bulk read error result = {:d} read size = {:d}",
				result, read_size);
			PLOGE << msg;
			throw std::runtime_error(msg);
		}
	}
}

void USBDevice::control_i2c(I2CBuffer& i2c_buffer)
{
	if (i2c_buffer.mode() != I2CBuffer::Mode::None)
	{
		control_usb(i2c_buffer.send_buf(), i2c_buffer.recv_buf());
	}
}

void USBDevice::reset_usb() const
{
	PLOGV << __func__;

	WinUsb_AbortPipe(winusb_handle_, USB_ENDPOINT_READ);
	WinUsb_ResetPipe(winusb_handle_, USB_ENDPOINT_READ);
	WinUsb_AbortPipe(winusb_handle_, USB_ENDPOINT_WRITE);
	WinUsb_ResetPipe(winusb_handle_, USB_ENDPOINT_WRITE);
}

bool USBDevice::has_vid_pid(uint16_t vid, uint16_t pid) const
{
	PLOGD << std::format("vid = {:#06x} pid = {:#06x}", vid, pid);

	if (vid == USB_VID_T230 &&
		(pid == USB_PID_T230A || pid == USB_PID_T230C2))
	{
		return true;
	}

	return false;
}

}
