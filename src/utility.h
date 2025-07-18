// SPDX-License-Identifier: GPL-2.0-only
/*
	utility.h

	Copyright (C) 2024 hendecarows
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>
#include <source_location>

#include <windows.h>


namespace utility
{

template<typename T>
std::string hex_dump(const std::vector<T>& buf)
{
	std::string s;
	for (const auto& p : buf)
	{
		s.append(std::vformat(" {:02x}", std::make_format_args(p)));
	}

	return s;
}

template<typename T>
std::string format_join(const std::vector<T>& buf, const std::string& fmt)
{
	std::string s;
	for (const auto& p : buf)
	{
		s.append(std::vformat(fmt, std::make_format_args(p)));
	}

	return s;
}

inline std::string current(const std::source_location& current = std::source_location::current())
{
	// 現在行のファイル名と行番号の文字列を返す関数
	return std::format("{}@{:d}", current.file_name(), current.line());
}

GUID parse_guid(const std::string& str);
bool utf8_to_utf16(const std::string& utf8, std::wstring& utf16);
bool utf16_to_utf8(const std::wstring& utf16, std::string& utf8);
std::wstring utf8to16(const std::string& utf8);

}
