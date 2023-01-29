#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include "Common/NtBase.hpp"
#include "Common/NtHandle.hpp"
#include "Common/NtEvent.hpp"
#include "Common/CriticalSection.hpp"
#include "Common/NtUtils.hpp"
#include <algorithm> // std::min and std::max.

#ifdef _CONSOLE

extern bool g_trace_log;

inline void DebugLogImpl(const char * funcname, const char * format, ...)
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

	if (g_trace_log && funcname)
	{
		printf("[%s] ", funcname);
	}

	va_list argptr;
	va_start(argptr, format);
	vprintf(format, argptr);

	printf("\n");
	fflush(stdout);
}

#define DebugLog(...) DebugLogImpl(__FUNCTION__, __VA_ARGS__)
#define DebugLogWarning(...) DebugLogImpl(__FUNCTION__, "WARNING: " __VA_ARGS__)
#define DebugLogError(...) DebugLogImpl(__FUNCTION__, "ERROR: " __VA_ARGS__)
#define TraceLog(...) do { if (g_trace_log) DebugLogImpl(__FUNCTION__, __VA_ARGS__); } while(0)

#else

#define DebugLog(...)
#define DebugLogWarning(...)
#define DebugLogError(...)
#define TraceLog(...)

#endif
