// SPDX-License-Identifier: GPL-2.0-only
/*
	usb_request_stream.h

	Copyright (C) 2024 hendecarows
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

#include <windows.h>


namespace t230
{
class USBRequest
{
public:
	USBRequest();
	~USBRequest();

	bool& is_waiting() { return is_waiting_; }
	OVERLAPPED& overlapped() { return overlapped_; }
	HANDLE& handle_event() { return overlapped_.hEvent; }
	std::vector<uint8_t>& buffer() { return buffer_; }
	void clear();
	void reset(size_t buffer_size);
	void set_event() const;

private:
	bool is_waiting_;
	OVERLAPPED overlapped_;
	std::vector<uint8_t> buffer_;
};

class USBRequestStream
{
public:
	USBRequestStream() = default;
	~USBRequestStream() = default;

	USBRequestStream(const USBRequestStream&) = delete;
	USBRequestStream& operator=(const USBRequestStream&) = delete;
	USBRequestStream(USBRequestStream&&) = delete;
	USBRequestStream& operator=(USBRequestStream&&) = delete;

	size_t request_size() const { return request_size_; }
	size_t buffer_size() const { return buffer_size_; }
	std::list<USBRequest>& items() { return requests_; }
	USBRequest& current() { return *current_it_; }

	void clear();
	void resize(const size_t request_size, const size_t buffer_size);
	void reset();
	void move_begin();
	void move_next();
	void set();

private:
	size_t buffer_size_ = 0;
	size_t request_size_ = 0;
	std::list<USBRequest> requests_;
	std::list<USBRequest>::iterator current_it_;
};
}
