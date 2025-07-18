// SPDX-License-Identifier: GPL-2.0-only
/*
	usb_request_stream.cpp

	Copyright (C) 2024 hendecarows
*/

#include "usb_request_stream.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>

#include <windows.h>

#include "plog/Log.h"


namespace t230
{
USBRequest::USBRequest() :
	is_waiting_(false), overlapped_()
{
	clear();
}

USBRequest::~USBRequest()
{
	if (overlapped_.hEvent)
	{
		CloseHandle(overlapped_.hEvent);
		overlapped_.hEvent = nullptr;
	}
}

void USBRequest::clear()
{
	is_waiting_ = false;
	overlapped_.Internal = 0;
	overlapped_.InternalHigh = 0;
	overlapped_.Offset = 0;
	overlapped_.OffsetHigh = 0;

	if (!overlapped_.hEvent)
	{
		overlapped_.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		if (!overlapped_.hEvent)
		{
			auto msg = std::format(
				"CreateEvent() error for overlapped GetLastError = {:d}",
				GetLastError());
			PLOGE << msg;
			throw std::runtime_error(msg);
		}
	}
	ResetEvent(overlapped_.hEvent);
}

void USBRequest::reset(size_t buffer_size)
{
	clear();
	buffer_.resize(buffer_size);
}

void USBRequest::set_event() const
{
	if (overlapped_.hEvent)
	{
		SetEvent(overlapped_.hEvent);
	}
}

void USBRequestStream::clear()
{
	requests_.clear();
	current_it_ = requests_.begin();
}

void USBRequestStream::resize(const size_t request_size, const size_t buffer_size)
{
	requests_.resize(request_size);
	for (auto& it : requests_)
	{
		it.reset(buffer_size);
	}
	buffer_size_ = buffer_size;
	current_it_ = requests_.begin();
}

void USBRequestStream::reset()
{
	for (auto& it : requests_)
	{
		it.reset(buffer_size_);
	}
	current_it_ = requests_.begin();
}

void USBRequestStream::move_begin()
{
	current_it_ = requests_.begin();
}

void USBRequestStream::move_next()
{
	current_it_++;
	if (current_it_ == requests_.end())
	{
		current_it_ = requests_.begin();
	}
}

void USBRequestStream::set()
{
	current_it_->set_event();
}

}
