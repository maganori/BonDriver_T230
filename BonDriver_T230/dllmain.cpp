// SPDX-License-Identifier: GPL-2.0-only
/*
	dllmain.cpp
*/


#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <windows.h>
#include <libloaderapi.h>

#include "plog/Initializers/RollingFileInitializer.h"
#include "plog/Log.h"

#include "bon_driver.h"
#include "IBonDriver.h"


static std::filesystem::path configFileName;
static std::filesystem::path logFileName;

static bool GetDllFileName(HMODULE hModule, std::filesystem::path& filename)
{
	TCHAR path[MAX_PATH]{};

	auto size = GetModuleFileName(hModule, path, MAX_PATH);
	if (size == 0) { return false; }
	filename = path;

	return true;
}

#pragma warning(disable : 4273)
extern "C" __declspec(dllexport) IBonDriver * CreateBonDriver()
{
	auto p = std::make_unique<BonDriver>();
	if (p && p->LoadConfig(configFileName))
	{
		return p.release();
	}

	return nullptr;
}
#pragma warning(default : 4273)


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		if (!GetDllFileName(hModule, configFileName))
		{
			return FALSE;
		}
		logFileName = configFileName;
		configFileName.replace_extension(".toml");
		logFileName.replace_extension(".log");
		plog::init(plog::error, logFileName.wstring().c_str());
		break;

	case DLL_THREAD_ATTACH:
		break;

	case DLL_THREAD_DETACH:
		break;

	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
