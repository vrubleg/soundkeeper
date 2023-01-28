#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include "Common/NtBase.hpp"
#include "Common/NtHandle.hpp"
#include "Common/NtEvent.hpp"
#include "Common/CriticalSection.hpp"
#include "Common/NtUtils.hpp"
#include <algorithm> // std::min and std::max.

#ifdef _CONSOLE

inline void DebugLog(const char * format, ...)
{
	static CriticalSection mutex;
	ScopedLock lock(mutex);

	static uint64_t prev_date = 0;
	SYSTEMTIME now = {0};
	GetSystemTime(&now);

	// Output current date once. First 8 bytes are current date, so we can compare it as a 64-bit integer.
	if (prev_date != *((uint64_t*)&now))
	{
		prev_date = *((uint64_t*)&now);
		printf("%04d/%02d/%02d ", now.wYear, now.wMonth, now.wDay);
	}

	printf("%02d:%02d:%02d.%03d", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
	printf(" [%5d] ", GetThreadId(GetCurrentThread()));

	va_list argptr;
	va_start(argptr, format);
	vprintf(format, argptr);

	printf("\n");
	fflush(stdout);
}

#define DebugLogWarning(...) DebugLog("WARNING: " __VA_ARGS__)
#define DebugLogError(...) DebugLog("ERROR: " __VA_ARGS__)

#else

#define DebugLog(...)
#define DebugLogWarning(...)
#define DebugLogError(...)

#endif
