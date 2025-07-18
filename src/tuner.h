// SPDX-License-Identifier: GPL-2.0-only
/*
	tuner.h

	Copyright (C) 2024 hendecarows
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>

#include "i2c_buffer.h"


namespace t230
{
class Tuner
{
public:
	Tuner() = default;
	~Tuner();

	Tuner(const Tuner&) = delete;
	Tuner& operator=(const Tuner&) = delete;
	Tuner(Tuner&&) = delete;
	Tuner& operator=(Tuner&&) = delete;

	void set_i2c_function(std::function<void(I2CBuffer&)> i2c_func);
	void set_firmware(const std::vector<uint8_t>& buffer);
	void open_device();
	void close_device();
	void set_parameters(uint32_t frequency_hz);
	void wait_lock(std::chrono::milliseconds timeout = std::chrono::milliseconds(1500), bool is_required_lock = true);
	uint16_t get_rf_strength();

private:
	static constexpr uint32_t SI2141_CHIP_ID_A10 = ('A' << 24 | 41 << 16 | '1' << 8 | '0');
	static constexpr uint8_t I2C_ADDRESS_DEMODULATOR = 0x64;
	static constexpr uint8_t I2C_ADDRESS_TUNER = 0x60;
	static constexpr uint8_t DELIVERY_SYSTEM = 0x30;	// DVB-C ANNEX A,C
	static constexpr uint8_t BANDWIDTH = 0x06;			// 6MHz
	static constexpr uint8_t INVERSION = 0x00;
	static constexpr uint8_t IF_PORT = 0x00;
	static constexpr uint32_t IF_FREQUENCY = 5000000;
	static constexpr std::chrono::duration I2C_READ_TIMEOUT = std::chrono::milliseconds(500);

	std::atomic_bool is_open_ = false;
	std::function<void(I2CBuffer&)> i2c_function_;
	std::mutex i2c_mutex_;
	uint32_t chip_id_ = 0;					// 0x41293130 : 'A',41,'1','0'
	std::vector<uint8_t> chip_version_;		// 0x41,0x01,0x00      : 'A',1.0
	std::vector<uint8_t> firmware_version_;	// 0x41,0x06,0x00,0x0d : 'A',6.0.13
	std::vector<uint8_t> firmware_;
	uint32_t if_frequency_ = 0;

	void control_i2c(I2CBuffer& write_buf, I2CBuffer& read_buf);
	void init_device();
	void load_firmware();
	void sleep_device();
};
}
