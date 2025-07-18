// SPDX-License-Identifier: GPL-2.0-only
/*
	tuner.cpp

	Copyright (C) 2024 hendecarows
*/

#include "tuner.h"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <functional>
#include <format>
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

Tuner::~Tuner()
{
	PLOGV << __func__;
	close_device();
}

void Tuner::set_i2c_function(std::function<void(I2CBuffer&)> i2c_func)
{
	i2c_function_ = i2c_func;
}

void Tuner::set_firmware(const std::vector<uint8_t>& buffer)
{
	firmware_ = buffer;
}

void Tuner::open_device()
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

	init_device();
	is_open_ = true;
}

void Tuner::close_device()
{
	PLOGV << __func__;

	if (is_open_)
	{
		sleep_device();
		is_open_ = false;
	}
}

void Tuner::set_parameters(uint32_t frequency_hz)
{
	PLOGV << __func__;

	I2CBuffer write_buf(I2C_ADDRESS_TUNER);
	I2CBuffer read_buf(I2C_ADDRESS_TUNER);

	// delivery system and bandwidth
	PLOGD << "delivery system = " << std::hex << static_cast<int32_t>(DELIVERY_SYSTEM);
	PLOGD << "bandwidth = " << std::hex << static_cast<int32_t>(BANDWIDTH);
	write_buf.set_write({ 0x14, 0x00, 0x03, 0x07, DELIVERY_SYSTEM | BANDWIDTH, INVERSION });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	// if port
	PLOGD << "if port = " << std::hex << static_cast<int32_t>(IF_PORT);
	write_buf.set_write({ 0x14, 0x00, 0x02, 0x07, IF_PORT, 0x00 });
	read_buf.set_read(4, true);
	control_i2c(write_buf, read_buf);

	// if frequency
	if (if_frequency_ != IF_FREQUENCY)
	{
		PLOGD << "if frequency = " << IF_FREQUENCY;
		write_buf.set_write({
			0x14, 0x00, 0x06, 0x07,
			static_cast<uint8_t>((IF_FREQUENCY / 1000)),
			static_cast<uint8_t>((IF_FREQUENCY / 1000) >> 8)
		});
		read_buf.set_read(4, true);
		control_i2c(write_buf, read_buf);
		if_frequency_ = IF_FREQUENCY;
	}

	// channel frequency
	PLOGD << "channel frequency = " << frequency_hz << " (0x" << std::hex << frequency_hz << ") Hz";
	write_buf.set_write({
		0x41, 0x00, 0x00, 0x00,
		static_cast<uint8_t>(frequency_hz),
		static_cast<uint8_t>(frequency_hz >> 8),
		static_cast<uint8_t>(frequency_hz >> 16),
		static_cast<uint8_t>(frequency_hz >> 24),
	});
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);
}

void Tuner::wait_lock(std::chrono::milliseconds timeout, bool is_required_lock)
{
	PLOGD << __func__;
	using namespace std::chrono_literals;

	if (timeout == 0ms) { return; }

	I2CBuffer write_buf(I2C_ADDRESS_TUNER);
	I2CBuffer read_buf(I2C_ADDRESS_TUNER);

	write_buf.clear();
	auto has_lock = false;
	auto has_signal = false;
	auto status = 0;
	const auto timeout_point = std::chrono::steady_clock::now() + timeout;
	while (true)
	{
		read_buf.set_read(1, false);
		control_i2c(write_buf, read_buf);
		status = read_buf.recv_at(1);
		if (read_buf.has_ready() && !read_buf.has_error())
		{
			PLOGD << "lock status = " << std::hex << status;
			// at(1) & 0x85 : has lock
			// at(1) & 0x81 : has signal
			if ((status & 0x85) == 0x85)
			{
				has_lock = true;
				break;
			}

			if ((status & 0x81) == 0x81)
			{
				has_signal = true;
			}
		}
		if (std::chrono::steady_clock::now() > timeout_point) { break; }
		std::this_thread::sleep_for(10ms);
	}

	PLOGD << "has lock = " << has_lock;
	PLOGD << "has signal = " << has_signal;

	if (has_lock) { return; }
	if (has_signal && !is_required_lock) { return; }

	const auto msg = "tuner lock error";
	PLOGE << msg;
	throw std::runtime_error(msg);
}

uint16_t Tuner::get_rf_strength()
{
	PLOGV << __func__;

	I2CBuffer write_buf(I2C_ADDRESS_TUNER);
	I2CBuffer read_buf(I2C_ADDRESS_TUNER);
	uint16_t rssi = 0;

	write_buf.set_write({ 0x42, 0x00 });
	read_buf.set_read(12, false);
	control_i2c(write_buf, read_buf);

	if (read_buf.has_ready() && !read_buf.has_error())
	{
		auto strength = std::clamp(static_cast<int32_t>(read_buf.recv_at(4)) + 100, 0, 80);
		rssi = static_cast<uint16_t>(strength * 0xffff / 80);
		PLOGD << "rf streangth = " << strength << " rssi = " << rssi;
	}

	return rssi;
}

void Tuner::control_i2c(I2CBuffer& write_buf, I2CBuffer& read_buf)
{
	using namespace std::chrono_literals;
	I2CBuffer open_gate(I2C_ADDRESS_DEMODULATOR, { 0xc0, 0x0d, 0x01 });
	I2CBuffer close_gate(I2C_ADDRESS_DEMODULATOR, { 0xc0, 0x0d, 0x00 });
	std::unique_lock<std::mutex> lock(i2c_mutex_);

	if (write_buf.mode() == I2CBuffer::Mode::Write)
	{
		i2c_function_(open_gate);
		i2c_function_(write_buf);
		i2c_function_(close_gate);
	}

	if (read_buf.mode() == I2CBuffer::Mode::Read)
	{
		const auto timeout = std::chrono::steady_clock::now() + I2C_READ_TIMEOUT;

		while (true)
		{
			i2c_function_(open_gate);
			i2c_function_(read_buf);
			i2c_function_(close_gate);
			if (read_buf.has_ready() && !read_buf.has_error()) { break; }
			if (std::chrono::steady_clock::now() > timeout) { break; }
			std::this_thread::sleep_for(10ms);
		}

		if (read_buf.check_status())
		{
			if (!read_buf.has_ready() || read_buf.has_error())
			{
				auto msg = std::format(
					"i2c transfer error send = {} recv = {}",
					utility::hex_dump(read_buf.send_buf()), utility::hex_dump(read_buf.recv_buf()));
				PLOGE << msg;
				throw std::runtime_error(msg);
			}
		}
	}
}

void Tuner::init_device()
{
	PLOGV << __func__;

	I2CBuffer write_buf(I2C_ADDRESS_TUNER);
	I2CBuffer read_buf(I2C_ADDRESS_TUNER);

	// get xtal trim property
	PLOGV << "xtal trim property";
	write_buf.set_write({ 0x15, 0x00, 0x02, 0x04 });
	read_buf.set_read(4, false);
	control_i2c(write_buf, read_buf);
	PLOGD << "tuner xtal trim = " << utility::hex_dump(read_buf.recv_buf());

	// power on
	PLOGV << "clock mode enable";
	write_buf.set_write({ 0xc0, 0x00, 0x0d, 0x0e, 0x00, 0x01, 0x01, 0x01, 0x01, 0x03 });
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);

	// wake up
	PLOGV << "wake up";
	write_buf.set_write({ 0xc0, 0x08, 0x01, 0x02, 0x00, 0x00, 0x01 });
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);

	// query chip id
	PLOGV << "query chip id";
	write_buf.set_write({ 0x02, });
	read_buf.set_read(13, true);
	control_i2c(write_buf, read_buf);

	// 0x41293130 : 'A',41,'1','0'
	chip_id_ = (read_buf.recv_at(2) << 24) | (read_buf.recv_at(3) << 16) | (read_buf.recv_at(4) << 8) | read_buf.recv_at(5);
	PLOGD << "tuner chip id = " << std::hex << chip_id_;
	if (chip_id_ != SI2141_CHIP_ID_A10)
	{
		auto msg = std::format(
			"unknown chip id = {:x}",
			chip_id_);
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	// 0x41,0x01,0x00 : 'A',1.0
	chip_version_ = {
		read_buf.recv_at(2),
		static_cast<uint8_t>(read_buf.recv_at(4) - 0x30),
		static_cast<uint8_t>(read_buf.recv_at(5) - 0x30),
		read_buf.recv_at(6)
	};
	PLOGD << "tuner chip version = " << utility::hex_dump(chip_version_);

	load_firmware();

	// enable tuner status flags
	PLOGV << "enable tuner status";
	write_buf.set_write({ 0x14, 0x00, 0x01, 0x05, 0x01, 0x00 });
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x14, 0x00, 0x01, 0x06, 0x01, 0x00 });
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);

	write_buf.set_write({ 0x14, 0x00, 0x01, 0x07, 0x01, 0x00 });
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);
}

void Tuner::load_firmware()
{
	PLOGV << "begin load formware";

	// filename: dvb-tuner-si2141-a10-01.fw
	// file size: 2159 (2159 % 17 == 0)
	const auto CHUNKED_SIZE = 17;
	const auto remainder = firmware_.size() % CHUNKED_SIZE;

	if (remainder != 0)
	{
		auto msg = std::format(
			"unknown format firmware: {:d} % {:d} != 0",
			firmware_.size(), CHUNKED_SIZE);
		PLOGE << msg;
		throw std::runtime_error(msg);
	}

	I2CBuffer write_buf(I2C_ADDRESS_TUNER);
	I2CBuffer read_buf(I2C_ADDRESS_TUNER);
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
	PLOGV << "end load formware";

	// run new firmware
	PLOGV << "run new firmware";
	write_buf.set_write({ 0x01, 0x01 });
	control_i2c(write_buf, read_buf);

	// query firmware version
	PLOGV << "query firmware version";
	write_buf.set_write({ 0x11, });
	read_buf.set_read(10, true);
	control_i2c(write_buf, read_buf);

	firmware_version_ = {
		read_buf.recv_at(10),
		static_cast<uint8_t>(read_buf.recv_at(7) - 0x30),
		static_cast<uint8_t>(read_buf.recv_at(8) - 0x30),
		read_buf.recv_at(9)
	};

	// check firmware version
	auto ver = std::format(
		"{:c} {:d}.{:d}.{:d}",
		firmware_version_.at(0),
		firmware_version_.at(1),
		firmware_version_.at(2),
		firmware_version_.at(3)
	);
	PLOGD << "firmware version: " << ver;

	if (firmware_version_.at(0) == chip_version_.at(0) &&
		firmware_version_.at(1) == chip_version_.at(1))
	{
		return;
	}

	auto msg = std::format(
		"unknown firmware version = {}",
		ver);
	PLOGE << msg;
	throw std::runtime_error(msg);
}

void Tuner::sleep_device()
{
	PLOGD << __func__;

	I2CBuffer write_buf(I2C_ADDRESS_TUNER);
	I2CBuffer read_buf(I2C_ADDRESS_TUNER);

	write_buf.set_write({ 0x16, 0x00 });
	read_buf.set_read(1, true);
	control_i2c(write_buf, read_buf);
}

}
