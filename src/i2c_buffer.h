// SPDX-License-Identifier: GPL-2.0-only
/*
	i2c_buffer.h

	Copyright (C) 2024 hendecarows
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <vector>

#include "plog/Log.h"

namespace t230
{
class I2CBuffer
{
public:
	I2CBuffer(uint8_t address) :
		address_(address)
	{}
	I2CBuffer(uint8_t address, const std::vector<uint8_t> write_buf) :
		address_(address)
	{
		set_write(write_buf);
	}
	I2CBuffer(uint8_t address, const size_t read_size) :
		address_(address)
	{
		set_read(read_size, true);
	}
	~I2CBuffer() = default;

	enum class Mode : int32_t
	{
		None = 0,
		Read = 1,
		Write = 2,
	};

	Mode mode() const { return mode_; }
	bool check_status() const { return check_status_; }
	uint8_t address() const { return address_; }
	size_t read_size() const { return read_size_; }
	size_t write_size() const { return write_size_; }
	uint8_t status_code() const { return (recv_buf_.size() >= 2) ? recv_buf_.at(1) : 0; }
	bool has_ready() const { return status_code() & 0x80 ? true : false; }
	bool has_error() const { return status_code() & 0x40 ? true : false; }
	std::vector<uint8_t>& send_buf() { return send_buf_; }
	std::vector<uint8_t>& recv_buf() { return recv_buf_; }
	uint8_t send_at(size_t idx) const { return send_buf_.at(idx); }
	uint8_t recv_at(size_t idx) const { return recv_buf_.at(idx); }

	void clear()
	{
		mode_ = Mode::None;
		read_size_ = 0;
		write_size_ = 0;
		check_status_ = true;
		send_buf_.clear();
		recv_buf_.clear();
	}

	void set_write(const std::vector<uint8_t>& write_buf)
	{
		if (write_buf.size() == 0 || write_buf.size() > MAX_DATA_SIZE)
		{
			const auto msg = std::format(
				"i2c data write size error size={:d} max={:d}",
				write_buf.size(), MAX_DATA_SIZE);
			PLOGE << msg;
			throw std::runtime_error(msg);
		}

		clear();
		mode_ = Mode::Write;
		send_buf_ = {
			0x08,
			address(),
			static_cast<uint8_t>(write_buf.size()),
		};
		send_buf_.insert(send_buf_.end(), write_buf.begin(), write_buf.end());
		recv_buf_.resize(1);
		write_size_ = write_buf.size();
	}

	void set_read(const size_t read_size, const bool check_status)
	{
		if (read_size > MAX_DATA_SIZE)
		{
			const auto msg = std::format(
				"i2c data read size error size={:d} > max={:d}",
				read_size, MAX_DATA_SIZE);
			PLOGE << msg;
			throw std::runtime_error(msg);
		}

		clear();
		mode_ = Mode::Read;
		check_status_ = check_status;
		if (read_size > 0)
		{
			send_buf_ = {
				0x09,
				0,
				static_cast<uint8_t>(read_size),
				address()
			};
			recv_buf_.resize(read_size + 1);
			read_size_ = read_size;
		}
	}

private:
	static constexpr size_t MAX_BUF_SIZE = 64;
	static constexpr size_t MAX_DATA_SIZE = MAX_BUF_SIZE - 4;	// 60

	Mode mode_ = Mode::None;
	bool check_status_ = true;
	uint8_t address_ = 0;
	size_t read_size_ = 0;
	size_t write_size_ = 0;
	std::vector<uint8_t> send_buf_;
	std::vector<uint8_t> recv_buf_;
};
}
