// SPDX-License-Identifier: GPL-2.0-only
/*
	demodulator.cpp

	Copyright (C) 2024 hendecarows
*/

#include "demodulator.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <format>
#include <functional>
#include <ios>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "plog/Log.h"

#include "i2c_buffer.h"
#include "utility.h"

namespace t230
{

Demodulator::~Demodulator()
{
	PLOGV << __func__;

	close_device();
}

void Demodulator::set_i2c_function(std::function<void(I2CBuffer&)> i2c_func)
{
	i2c_function_ = i2c_func;
}

void Demodulator::set_firmware(const std::vector<uint8_t>& buffer)
{
	firmware_ = buffer;
}

void Demodulator::open_device()
{
	PLOGV << __func__;

	if (is_open_) { return; }

	if (!i2c_function_)
	{
		auto msg = "empty i2c function";
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	if (firmware_.empty())
	{
		auto msg = "empty firmware";
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	probe_device();
	init_device();
	is_open_ = true;
}

void Demodulator::close_device()
{
	PLOGV << __func__;

	if (is_open_)
	{
		control_ts_bus(false);
		sleep_device();
		is_open_ = false;
	}
}

void Demodulator::set_frontend()
{
	PLOGV << __func__;

	I2CBuffer write_buf(I2C_ADDRESS);
	I2CBuffer read_buf(I2C_ADDRESS);

	write_buf.set_write({ 0x88, 0x02, 0x02, 0x02, 0x02 });
	read_buf.set_read(5, true);
	control_i2c(write_buf, read_buf);

	// dvb-c annex a,c
	PLOGV << "dvb-c annex a,c";
	write_buf.set_write({ 0x89, 0x21, 0x06, 0x11, 0x89, 0xf0 });
	read_buf.set_read(3, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x51, 0x03 });
	read_buf.set_read(12, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x12, 0x08, 0x04 });
	read_buf.set_read(3, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x14, 0x00, 0x0c, 0x10, 0x12, 0x00 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x14, 0x00, 0x06, 0x10, 0x24, 0x00 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x14, 0x00, 0x07, 0x10, 0x00, 0x24 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	PLOGD << "delivery system = " << std::hex << static_cast<int32_t>(DELIVERY_SYSTEM);
	PLOGD << "bandwidth = " << std::hex << static_cast<int32_t>(BANDWIDTH);
	uint8_t val4 = DELIVERY_SYSTEM | BANDWIDTH;
	uint8_t val5 = SPECTRAL_INVERSION ? 0x01 : 0x00;
	write_buf.set_write({ 0x14, 0x00, 0x0a, 0x10, val4, val5 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	// dvb-c symbol rate
	PLOGD << "dvb-c symbol rate = " << SYMBOL_RATE;
	val4 = static_cast<uint8_t>(SYMBOL_RATE);
	val5 = static_cast<uint8_t>(SYMBOL_RATE >> 8);
	write_buf.set_write({ 0x14, 0x00, 0x02, 0x11, val4, val5 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x14, 0x00, 0x0f, 0x10, 0x10, 0x00 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	val5 = TS_CLOCK_INV ? 0x08 : 0x18;
	write_buf.set_write({ 0x14, 0x00, 0x09, 0x10, 0xe3, val5 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	val5 = TS_CLOCK_INV ? 0x05 : 0x15;
	write_buf.set_write({ 0x14, 0x00, 0x08, 0x10, 0xd7, val5 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x14, 0x00, 0x01, 0x12, 0x00, 0x00 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x14, 0x00, 0x01, 0x03, 0x0c, 0x00 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x85 });
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);
}

void Demodulator::wait_lock(std::chrono::milliseconds timeout, bool is_required_lock)
{
	PLOGV << __func__;
	using namespace std::chrono_literals;

	if (timeout == 0ms) { return;  }

	I2CBuffer write_buf(I2C_ADDRESS);
	I2CBuffer read_buf(I2C_ADDRESS);

	// dvb-c annex a,c
	write_buf.set_write({ 0x90, 0x01 });

	auto has_lock = false;
	auto has_signal = false;
	auto status = 0;
	const auto timeout_point = std::chrono::steady_clock::now() + timeout;
	while (true)
	{
		read_buf.set_read(9, false);
		control_i2c(write_buf, read_buf);
		status = read_buf.recv_at(3);
		if (read_buf.has_ready() && !read_buf.has_error())
		{
			PLOGD << "lock status = " << std::hex << status;
			if (status & 0x02)
			{
				has_signal = true;
			}
			if (status & 0x04)
			{
				has_lock = true;
				break;
			}
		}
		if (std::chrono::steady_clock::now() > timeout_point) { break; }
		std::this_thread::sleep_for(10ms);
	}

	PLOGD << "has lock = " << has_lock;
	PLOGD << "has signal = " << has_signal;

	if (has_lock) { return; }
	if (has_signal && !is_required_lock) { return; }

	auto msg = "demodulator lock error";
	PLOGE << msg;
	throw std::runtime_error(msg);
}

void Demodulator::control_ts_bus(bool is_enable)
{
	PLOGV << __func__;

	I2CBuffer write_buf(I2C_ADDRESS);
	I2CBuffer read_buf(I2C_ADDRESS);

	uint8_t val = TS_MODE;
	if (val & TS_CLOCK_MANUAL)
	{
		write_buf.set_write({ 0x14, 0x00, 0x0d, 0x10, 0xe8, 0x03 });
		read_buf.set_read(4, true);
		control_i2c(write_buf, read_buf);
	}

	// set TS_MODE property
	if (val & TS_CLOCK_MANUAL)
	{
		val = TS_CLOCK_MANUAL;
	}

	if (is_enable)
	{
		val |= TS_MODE;
	}
	else
	{
		val |= TS_TRISTATE;
	}

	if (TS_CLOCK_GAPPED)
	{
		val |= 0x40;
	}

	write_buf.set_write({ 0x14, 0x00, 0x01, 0x10, val, 0x00 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);
}

float Demodulator::read_cnr()
{
	if (!is_open_) { return 0.0f; }

	I2CBuffer write_buf(I2C_ADDRESS);
	I2CBuffer read_buf(I2C_ADDRESS);

	write_buf.set_write({ 0x90, 0x01 });
	read_buf.set_read(9, true);
	control_i2c(write_buf, read_buf);
	if ((read_buf.recv_at(3) & 0x06) == 0x06)
	{
		return static_cast<float>(read_buf.recv_at(4) / 4.0);
	}

	return 0.0f;
}

void Demodulator::control_i2c(I2CBuffer& write_buf, I2CBuffer& read_buf)
{
	using namespace std::chrono_literals;
	std::unique_lock<std::mutex> lock(i2c_mutex_);

	if (write_buf.mode() == I2CBuffer::Mode::Write)
	{
		i2c_function_(write_buf);
	}

	if (read_buf.mode() == I2CBuffer::Mode::Read)
	{
		const auto timeout = std::chrono::steady_clock::now() + I2C_READ_TIMEOUT;

		while (true)
		{
			i2c_function_(read_buf);
			if (read_buf.has_ready() && !read_buf.has_error()) { break; }
			if (std::chrono::steady_clock::now() > timeout) { break; }
			std::this_thread::sleep_for(10ms);
		}

		if (read_buf.check_status())
		{
			if (!read_buf.has_ready() || read_buf.has_error())
			{
				auto msg = std::format(
					"i2c error send = {} recv = {}",
					utility::format_join(read_buf.send_buf(), " {:02x}"),
					utility::format_join(read_buf.recv_buf(), " {:02x}"));
				PLOGE << msg;
				throw std::runtime_error(msg);
			}
		}
	}
}

void Demodulator::probe_device()
{
	PLOGV << __func__;

	I2CBuffer write_buf(I2C_ADDRESS);
	I2CBuffer read_buf(I2C_ADDRESS);

	// initialize
	PLOGV << "initialize";
	write_buf.set_write({ 0xc0, 0x12, 0x00, 0x0c, 0x00, 0x0d, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
	read_buf.clear();
	control_i2c(write_buf, read_buf);

	// power on
	PLOGV << "power on";
	write_buf.set_write({ 0xc0, 0x06, 0x01, 0x0f, 0x00, 0x20, 0x20, 0x01 });
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);

	// chip id
	PLOGV << "query chip id";
	write_buf.set_write({ 0x02, });
	read_buf.set_read(13, true);
	control_i2c(write_buf, read_buf);

	// 0x44443630 : 'D',68,'6','0'
	chip_id_ = (read_buf.recv_at(2) << 24) | (read_buf.recv_at(3) << 16) | (read_buf.recv_at(4) << 8) | read_buf.recv_at(5);
	PLOGD << "demodulator chip id = " << std::hex << chip_id_;
	if (chip_id_ != SI2168_CHIP_ID_D60)
	{
		auto msg = std::format(
			"unknown chip id = {:x}",
			chip_id_);
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	// 0x44,0x06,0x00,0x01 : 'D',6.0.1
	chip_version_ = {
		read_buf.recv_at(2),
		static_cast<uint8_t>(read_buf.recv_at(4) - 0x30),
		static_cast<uint8_t>(read_buf.recv_at(5) - 0x30),
		read_buf.recv_at(6)
	};
	PLOGD << "demodulator chip version = " << utility::hex_dump(chip_version_);
}

void Demodulator::init_device()
{
	PLOGV << __func__;

	I2CBuffer write_buf(I2C_ADDRESS);
	I2CBuffer read_buf(I2C_ADDRESS);

	// initialize
	PLOGV << "initialize";
	write_buf.set_write({ 0xc0, 0x12, 0x00, 0x0c, 0x00, 0x0d, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, });
	read_buf.clear();
	control_i2c(write_buf, read_buf);

	// power up
	PLOGV << "power up";
	write_buf.set_write({ 0xc0, 0x06, 0x01, 0x0f, 0x00, 0x20, 0x20, 0x01, });
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);

	// load firmware
	load_firmware();
}

void Demodulator::load_firmware()
{
	PLOGV << __func__;

	// filename: dvb-demod-si2168-d60-01.fw
	// file size: 10,829 (10829 % 17 == 0)
	const auto CHUNKED_SIZE = 17;
	const auto remainder = firmware_.size() % CHUNKED_SIZE;

	if (remainder != 0)
	{
		auto msg = std::format(
			"invalid firmware size : {:d} % {:d} != 0",
			firmware_.size(), CHUNKED_SIZE);
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	I2CBuffer write_buf(I2C_ADDRESS);
	I2CBuffer read_buf(I2C_ADDRESS);
	std::vector<uint8_t> firmware_buf;

	for (auto p = firmware_.cbegin(); p != firmware_.cend(); p += CHUNKED_SIZE)
	{
		auto len = *p;
		if (len > CHUNKED_SIZE - 1)
		{
			auto msg = std::format(
				"unknown format firmware: data[0] = {:d}",
				len);
			PLOGE << msg;
			throw std::runtime_error(msg);
		}

		firmware_buf.assign(p + 1, p + len + 1);
		write_buf.set_write(firmware_buf);
		read_buf.set_read(1, true);
		control_i2c(write_buf, read_buf);
	}

	// run new firmware
	PLOGV << "run new firmware";
	write_buf.set_write({ 0x01, 0x01, });
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);

	// query firmware version
	PLOGV << "query firmware version";
	write_buf.set_write({ 0x11, });
	read_buf.set_read(10, true);
	control_i2c(write_buf, read_buf);

	// 0x44,0x06,0x00,0x0d : 'D',6,0,13
	firmware_version_ = {
		static_cast<uint8_t>(read_buf.recv_at(10) + 0x40),
		static_cast<uint8_t>(read_buf.recv_at(7) - 0x30),
		static_cast<uint8_t>(read_buf.recv_at(8) - 0x30),
		read_buf.recv_at(9)
	};

	// check firmware version
	auto ver = std::format(
		"{:c}.{:d}.{:d}.{:d}",
		firmware_version_.at(0),
		firmware_version_.at(1),
		firmware_version_.at(2),
		firmware_version_.at(3)
	);
	PLOGD << "firmware version = " << ver;

	PLOGV << "check firmware version";
	if (firmware_version_.at(0) != chip_version_.at(0) ||
		firmware_version_.at(1) != chip_version_.at(1))
	{
		auto msg = std::format(
			"unknown firmware version={}",
			ver);
		PLOGE << msg;
		throw std::runtime_error(msg);
	}
}

void Demodulator::sleep_device()
{
	PLOGV << __func__;

	control_ts_bus(false);

	I2CBuffer write_buf(I2C_ADDRESS);
	I2CBuffer read_buf(I2C_ADDRESS);

	write_buf.set_write({ 0x13, });
	control_i2c(write_buf, read_buf);
}

}
