// SPDX-License-Identifier: GPL-2.0-only
/*
	bon_driver.h

	Copyright (C) 2024 hendecarows
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

#include "toml.hpp"

#include "IBonDriver2.h"
#include "demodulator.h"
#include "tsmf_parser.h"
#include "tuner.h"
#include "usb_bridge.h"


class BonDriver final : public IBonDriver2
{
public:
	BonDriver();
	~BonDriver();

	// IBonDriver
	const BOOL OpenTuner(void) override;
	void CloseTuner(void) override;
	const BOOL SetChannel(const BYTE bCh) override;
	const float GetSignalLevel(void) override;
	const DWORD WaitTsStream(const DWORD dwTimeOut = 0) override;
	const DWORD GetReadyCount(void) override;
	const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain) override;
	const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain) override;
	void PurgeTsStream(void) override;
	void Release(void) override;

	// IBonDriver2
	LPCTSTR GetTunerName(void) override;
	const BOOL IsTunerOpening(void) override;
	LPCTSTR EnumTuningSpace(const DWORD dwSpace) override;
	LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel) override;
	const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel) override;
	const DWORD GetCurSpace(void) override;
	const DWORD GetCurChannel(void) override;

	bool LoadConfig(const std::filesystem::path& filename);

private:
	std::atomic_bool is_open_ = false;
	std::atomic_bool is_streaming_ = false;
	DWORD current_space_ = 0;
	DWORD current_channel_ = 0;
	std::mutex mutex_;
	std::thread streaming_thread_;

	toml::value config_;
	std::vector<uint8_t> demodulator_firmware_;
	std::vector<uint8_t> tuner_firmware_;
	std::unique_ptr<std::vector<uint8_t>> stream_buffer_;

	t230::USBBridge usb_bridge_;
	t230::Demodulator demodulator_;
	t230::Tuner tuner_;
	ts::TsmfParser tsmf_parser_;

	std::vector<uint8_t> ReadFirmware(const std::filesystem::path& filename) const;
	void ParseTsmf(void);
};
