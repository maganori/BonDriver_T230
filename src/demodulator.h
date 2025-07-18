// SPDX-License-Identifier: GPL-2.0-only
/*
	demodulator.h

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
class Demodulator
{
public:
	Demodulator() = default;
	~Demodulator();

	Demodulator(const Demodulator&) = delete;
	Demodulator& operator=(const Demodulator&) = delete;
	Demodulator(Demodulator&&) = delete;
	Demodulator& operator=(Demodulator&&) = delete;

	void set_i2c_function(std::function<void(I2CBuffer&)> i2c_func);
	void set_firmware(const std::vector<uint8_t>& firmware);
	void open_device();
	void close_device();
	void set_frontend();
	void wait_lock(std::chrono::milliseconds timeout = std::chrono::milliseconds(1500), bool is_required_lock = true);
	void control_ts_bus(bool is_enable);
	float read_cnr();

private:
	static constexpr uint32_t SI2168_CHIP_ID_D60 = ('D' << 24 | 68 << 16 | '6' << 8 | '0');
	static constexpr uint8_t I2C_ADDRESS = 0x64;
	static constexpr uint8_t TS_PARALLEL = 0x06;
	static constexpr uint8_t TS_SERIAL = 0x03;
	static constexpr uint8_t TS_TRISTATE = 0x00;
	static constexpr uint8_t TS_CLOCK_MANUAL = 0x20;
	static constexpr uint8_t TS_MODE = TS_PARALLEL | TS_CLOCK_MANUAL;
	static constexpr uint8_t DELIVERY_SYSTEM = 0x30;	// DVB-C ANNEX A,C
	static constexpr uint8_t BANDWIDTH = 0x06;			// 6 MHz
	static constexpr uint16_t SYMBOL_RATE = 5274;		// 5274 kbaud
	static constexpr bool TS_CLOCK_INV = true;
	static constexpr bool TS_CLOCK_GAPPED = false;
	static constexpr bool SPECTRAL_INVERSION = false;
	static constexpr std::chrono::duration I2C_READ_TIMEOUT = std::chrono::milliseconds(500);

	std::atomic_bool is_open_ = false;
	std::function<void(I2CBuffer&)> i2c_function_;
	std::mutex i2c_mutex_;
	uint32_t chip_id_ = 0;					// 0x44443630 : 'D' << 24 | 68 << 16 | '6' << 8 | '0'
	std::vector<uint8_t> chip_version_;		// 0x44,0x06,0x00,0x01 : 'D',6,0,1
	std::vector<uint8_t> firmware_version_;	// 0x44,0x06,0x00,0x0d : 'D',6,0,13
	std::vector<uint8_t> firmware_;

	void control_i2c(I2CBuffer& write_buf, I2CBuffer& read_buf);
	void probe_device();
	void init_device();
	void load_firmware();
	void sleep_device();
};
}
