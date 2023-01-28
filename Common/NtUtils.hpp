#pragma once

#include "NtBase.hpp"
#include <initializer_list>

// ---------------------------------------------------------------------------------------------------------------------

extern "C" NTSYSAPI NTSTATUS WINAPI RtlGetVersion(PRTL_OSVERSIONINFOW lpVersionInformation);

inline uint32_t GetNtBuildNumber()
{
	static uint32_t build_number = 0;

	if (build_number != 0)
	{
		return build_number;
	}

	RTL_OSVERSIONINFOW os_info = { 0 };
	os_info.dwOSVersionInfoSize = sizeof(os_info);
	RtlGetVersion(&os_info);
	build_number = os_info.dwBuildNumber;
	return build_number;
}

// ---------------------------------------------------------------------------------------------------------------------
