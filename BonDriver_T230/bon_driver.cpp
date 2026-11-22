// SPDX-License-Identifier: GPL-2.0-only
/*
	bon_driver.cpp

	Copyright (C) 2024 hendecarows
*/

#include "bon_driver.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <filesystem>
#include <format>
#include <iosfwd>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "plog/Log.h"

#include "utility.h"


BonDriver::BonDriver()
{
	PLOGV << __func__;
}

BonDriver::~BonDriver()
{
	PLOGV << __func__;

	CloseTuner();
}

void BonDriver::Release(void)
{
	PLOGV << __func__;

	delete this;
}

const BOOL BonDriver::OpenTuner(void)
{
	PLOGV << __func__;
	std::lock_guard<std::mutex> lock(mutex_);

	try
	{
		if (!is_open_)
		{
			demodulator_.set_i2c_function(usb_bridge_.get_i2c_function());
			tuner_.set_i2c_function(usb_bridge_.get_i2c_function());
			usb_bridge_.open_device();
			demodulator_.open_device();
			tuner_.open_device();
			is_open_ = true;
		}
		return TRUE;
	}
	catch (const std::exception& ex)
	{
		PLOGE << ex.what();
		return FALSE;
	}
}

void BonDriver::CloseTuner(void)
{
	PLOGV << __func__;
	std::lock_guard<std::mutex> lock(mutex_);

	if (!is_open_) { return; }

	try
	{
		tuner_.close_device();
		demodulator_.close_device();
		usb_bridge_.close_device();

		is_streaming_ = false;
		// (patched) same wake-before-join as SetChannel (see above).
		usb_bridge_.abort_stream_queue();
		if (streaming_thread_.joinable()) { streaming_thread_.join(); }
		tsmf_parser_.abort();

		is_open_ = false;
		current_space_ = 0;
		current_channel_ = 0;
	}
	catch (const std::exception& ex)
	{
		PLOGE << ex.what();
	}
}

const BOOL BonDriver::SetChannel(const BYTE bCh)
{
	return SetChannel(0, static_cast<DWORD>(bCh - 13));
}

const BOOL BonDriver::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	PLOGV << "dwSpace = " << dwSpace << " dwChannel = " << dwChannel;

	std::lock_guard<std::mutex> lock(mutex_);

	try
	{
		using namespace std::chrono_literals;

		auto& space = toml::find(config_, "space", dwSpace);
		auto& channel = toml::find(space, "channel", dwChannel);
		auto frequency_hz = toml::find<uint32_t>(channel, "frequency");
		auto tsmf_number = toml::find_or<int32_t>(channel, "tsmf_number", -1);
		PLOGD << "space = " << toml::find<std::string>(space, "name")
			<< " channel = " << toml::find<std::string>(channel, "name")
			<< " frequency = " << frequency_hz
			<< " tsmf number = " << tsmf_number;

		is_streaming_ = false;
		// (patched) wake ParseTsmf blocked in SyncQueue::pop() before joining,
		// otherwise the join waits out the 1000 ms pop timeout on every zap.
		usb_bridge_.abort_stream_queue();
		if (streaming_thread_.joinable()) { streaming_thread_.join(); }
		tsmf_parser_.abort();
		tsmf_parser_.clear();
		tsmf_parser_.set_tsmf_number(tsmf_number);

		demodulator_.control_ts_bus(false);
		usb_bridge_.control_stream(false);
		tuner_.set_parameters(frequency_hz);
		tuner_.wait_lock();
		demodulator_.set_frontend();
		demodulator_.wait_lock();
		demodulator_.control_ts_bus(true);
		usb_bridge_.control_stream(true);

		current_space_ = dwSpace;
		current_channel_ = dwChannel;
		is_streaming_ = true;
		streaming_thread_ = std::thread(&BonDriver::ParseTsmf, this);
		return TRUE;
	}
	catch (const std::exception& ex)
	{
		PLOGE << ex.what();
		return FALSE;
	}
}

const float BonDriver::GetSignalLevel(void)
{
	PLOGV << __func__;

	if (!is_streaming_)
	{
		return 0.0f;
	}
	return demodulator_.read_cnr();
}

const DWORD BonDriver::WaitTsStream(const DWORD dwTimeOut)
{
	PLOGV << "dwTimeOut = " << dwTimeOut;

	if (!is_streaming_)
	{
		return WAIT_ABANDONED;
	}

	return WAIT_OBJECT_0;
}

const DWORD BonDriver::GetReadyCount(void)
{
	PLOGV << __func__;

	if (!is_streaming_)
	{
		return 0;
	}

	return static_cast<DWORD>(usb_bridge_.queue_size());
}

const BOOL BonDriver::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	if (!is_streaming_)
	{
		return FALSE;
	}

	BYTE* pSrc = nullptr;
	if (GetTsStream(&pSrc, pdwSize, pdwRemain))
	{
		if (*pdwSize > 0)
		{
			std::memcpy(pDst, pSrc, *pdwSize);
		}
		return TRUE;
	}

	return FALSE;
}

const BOOL BonDriver::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	if (!is_streaming_)
	{
		return FALSE;
	}

	if (tsmf_parser_.stream_size() == 0)
	{
		*pdwSize = 0;
		*pdwRemain = 0;
		return TRUE;
	}

	if (tsmf_parser_.pop(stream_buffer_))
	{
		*ppDst = stream_buffer_->data();
		*pdwSize = static_cast<DWORD>(stream_buffer_->size());
		std::memcpy(*ppDst, stream_buffer_->data(), *pdwSize);
		*pdwRemain = static_cast<DWORD>(usb_bridge_.queue_size());
	}

	return TRUE;
}

void BonDriver::PurgeTsStream(void)
{
	PLOGV << __func__;
}

LPCTSTR BonDriver::GetTunerName(void)
{
	const static std::wstring tuner_name = L"BonDriver_T230";
	PLOGD << tuner_name;

	return tuner_name.c_str();
}

const BOOL BonDriver::IsTunerOpening(void)
{
	PLOGV << __func__;
	return is_open_;
}

LPCTSTR BonDriver::EnumTuningSpace(const DWORD dwSpace)
{
	PLOGD << "dwSpace = " << dwSpace;

	static std::wstring space_name;

	auto space_size = static_cast<DWORD>(config_.at("space").size());
	if (dwSpace >= space_size)
	{
		return nullptr;
	}

	auto& space = toml::find(config_, "space", dwSpace);
	space_name = utility::utf8to16(toml::find<std::string>(space, "name"));

	return space_name.c_str();
}

LPCTSTR BonDriver::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	PLOGD << "dwSpace = " << dwSpace << " dwChannel = " << dwChannel;

	static std::wstring channel_name;

	auto space_size = static_cast<DWORD>(config_.at("space").size());
	if (dwSpace >= space_size)
	{
		return nullptr;
	}

	auto& space = toml::find(config_, "space", dwSpace);
	auto channel_size = static_cast<DWORD>(space.at("channel").size());
	if (dwChannel >= channel_size)
	{
		return nullptr;
	}

	auto& channel = toml::find(space, "channel", dwChannel);
	channel_name = utility::utf8to16(toml::find<std::string>(channel, "name"));

	return channel_name.c_str();
}

const DWORD BonDriver::GetCurSpace(void)
{
	PLOGD << "current space = " << current_space_;

	return current_space_;
}

const DWORD BonDriver::GetCurChannel(void)
{
	PLOGD << "current channel = " << current_channel_;

	return current_channel_;
}

bool BonDriver::LoadConfig(const std::filesystem::path& filename)
{
	try
	{
		config_ = toml::parse(filename);

		// log
		auto log_level = plog::Severity::info;
		auto level = toml::find_or<int32_t>(config_, "device", "log", "level", static_cast<int32_t>(log_level));
		if (level >= plog::Severity::none && level <= plog::Severity::verbose)
		{
			log_level = static_cast<plog::Severity>(level);
		}
		plog::get()->setMaxSeverity(log_level);

		PLOGD << "config filename = " << filename.wstring();
		PLOGD << "device.log.level = " << level;

		// guid
		auto& guid = toml::find<std::string>(config_, "device", "guid");
		PLOGD << "device.guid = " << guid;
		usb_bridge_.set_guid(guid);

		// firmware config
		// demodulator firmware
		std::filesystem::path path_demod_fw(filename);
		std::filesystem::path file_demod_fw(toml::find<std::string>(config_, "device", "demodulator", "firmware"));
		path_demod_fw.replace_filename(file_demod_fw.filename());
		PLOGD << "device.demodulator.firmware = " << path_demod_fw.string();
		demodulator_.set_firmware(ReadFirmware(path_demod_fw));

		// tuner firmware
		std::filesystem::path path_tuner_fw(filename);
		std::filesystem::path file_tuner_fw(toml::find<std::string>(config_, "device", "tuner", "firmware"));
		path_tuner_fw.replace_filename(file_tuner_fw.filename());
		PLOGD << "device.tuner.firmware = " << path_tuner_fw.string();
		tuner_.set_firmware(ReadFirmware(path_tuner_fw));

		// stream config
		auto request_size = toml::find_or<size_t>(config_, "device", "stream", "request", "size", 16);
		PLOGD << "device.stream.request.size = " << request_size;
		usb_bridge_.set_request_size(request_size);
		auto transfer_size = toml::find_or<size_t>(config_, "device", "stream", "transfer", "size", 16384);
		PLOGD << "device.stream.transfer.size = " << transfer_size;
		usb_bridge_.set_transfer_size(transfer_size);
		auto queue_size = toml::find_or<size_t>(config_, "device", "stream", "queue", "size", 128);
		PLOGD << "device.stream.queue.size = " << queue_size;
		usb_bridge_.set_queue_size(queue_size);

		// tuning space
		if (!config_.contains("space"))
		{
			const auto msg = "undefined tuning space";
			PLOGE << msg;
			throw std::runtime_error(msg);
		}

		auto size = config_.at("space").size();
		for (size_t i = 0; i < size; i++)
		{
			auto& space = toml::find(config_, "space", i);
			if (!space.contains("name"))
			{
				const auto msg = "undefined tuning space name";
				PLOGE << msg;
				throw std::runtime_error(msg);
			}
			if (!space.contains("channel"))
			{
				const auto msg = "undefined channel in tuning space";
				PLOGE << msg;
				throw std::runtime_error(msg);
			}
		}
	}
	catch (const std::exception& ex)
	{
		PLOGE << ex.what();
		return false;
	}

	return true;
}

std::vector<uint8_t> BonDriver::ReadFirmware(const std::filesystem::path& filename) const
{
	std::ifstream ifs(filename, std::ios::in | std::ios::binary);
	if (!ifs.is_open())
	{
		const auto msg = std::format("failed to open firmware: {}", filename.string());
		PLOGE << msg;
		throw std::runtime_error(msg);
	}
	std::vector<uint8_t> firmware(
		(std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>()
	);
	if (firmware.size() == 0)
	{
		const auto msg = "tuner firmware size = 0";
		throw std::runtime_error(msg);
	}

	return firmware;
}

void BonDriver::ParseTsmf(void)
{
	std::unique_ptr<std::vector<uint8_t>> buffer;
	while (is_streaming_)
	{
		if (usb_bridge_.get_stream(buffer))
		{
			if (!tsmf_parser_.push(buffer))
			{
				PLOGW << "failed to push stream and lost stream size = " << buffer->size();
			}
		}
	}
}
