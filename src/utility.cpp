// SPDX-License-Identifier: GPL-2.0-only
/*
	utility.cpp

	Copyright (C) 2024 hendecarows
*/

#include "utility.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

#include <windows.h>


namespace utility
{

GUID parse_guid(const std::string& str)
{
	// GUID文字列からGUID構造体を返す関数
	// {3ba7eb50-f570-4356-977b-c7242157543c}

	auto msg = std::format("invalid argument of guid string={}", str);

	if (str.size() != 38 || str.at(0) != '{' ||
		str.at(9) != '-' || str.at(14) != '-' ||
		str.at(19) != '-' || str.at(24) != '-' ||
		str.at(37) != '}')
	{
		throw std::invalid_argument(msg);
	}

	GUID guid = {};

	// 最初の8バイト 3ba7eb50 を16進数で数値化
	guid.Data1 = std::stoul(str.substr(1, 8), nullptr, 16);

	// 2番目の4バイト f570 を16進数で数値化
	guid.Data2 = static_cast<uint16_t>(std::stoul(str.substr(10, 4), nullptr, 16));

	// 3番目の4バイト 4356 を16進数で数値化
	guid.Data3 = static_cast<uint16_t>(std::stoul(str.substr(15, 4), nullptr, 16));

	// 4,5番目の4,12バイト 977b-c7242157543c を1バイトずつ16進数で数値化
	// 97,7b,c7,24,21,57,54,3c
	auto data4 = str.substr(20, 4) + str.substr(25, 12);
	for (size_t i = 0, j = 0; i < data4.size(); i += 2, j++)
	{
		guid.Data4[j] = static_cast<uint8_t>(std::stoul(data4.substr(i, 2), nullptr, 16));
	}

	return guid;
}

bool utf8_to_utf16(const std::string& utf8, std::wstring& utf16)
{
	auto size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	if (size > 0)
	{
		utf16.resize(size);
		if (MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, utf16.data(), size))
		{
			return true;
		}
	}

	return false;
}

bool utf16_to_utf8(const std::wstring& utf16, std::string& utf8)
{
	auto size = WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (size > 0)
	{
		utf8.resize(size);
		if (WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), -1, utf8.data(), size, nullptr, nullptr))
		{
			return true;
		}
	}

	return false;
}

std::wstring utf8to16(const std::string& utf8)
{
	std::wstring utf16;
	utf8_to_utf16(utf8, utf16);
	return utf16;
}

}
