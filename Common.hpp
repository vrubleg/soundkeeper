#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include "Common/NtBase.hpp"
#include "Common/NtHandle.hpp"
#include "Common/NtEvent.hpp"
#include "Common/CriticalSection.hpp"
#include "Common/NtUtils.hpp"
#include "Common/StrUtils.hpp"
#include <algorithm> // std::min and std::max.

#ifdef _CONSOLE

inline void DebugLogImpl(const char * funcname, const char * type, const char * format, ...)
{
	static CriticalSection mutex;
	ScopedLock lock(mutex);

	static bool is_inited = false;
	static bool show_trace = false;

	if (!is_inited)
	{
		// It also looks in path to exe, but it's fine for a debug build.
		char buf[MAX_PATH];
		strcpy_s(buf, GetCommandLineA());
		_strlwr(buf);
		if (StringContainsNoCase(buf, "trace")) { show_trace = true; }
		is_inited = true;
	}

	if (!show_trace && type && StringEqualsNoCase(type, "TRACE")) { return; }

	static uint64_t prev_date = 0;
	SYSTEMTIME now = {0};
	GetSystemTime(&now);

	// Output current date once. First 8 bytes are current date, so we can compare it as a 64-bit integer.
	if (prev_date != *((uint64_t*)&now))
	{
		prev_date = *((uint64_t*)&now);
		printf("%04d/%02d/%02d ", now.wYear, now.wMonth, now.wDay);
	}

	printf("%02d:%02d:%02d.%03d ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
	printf("[%5d] ", GetThreadId(GetCurrentThread()));

	if (show_trace && funcname)
	{
		printf("[%s] ", funcname);
	}

	if (type && !StringEqualsNoCase(type, "TRACE") && !StringEqualsNoCase(type, "INFO"))
	{
		printf("[%s] ", type);
	}

	va_list argptr;
	va_start(argptr, format);
	vprintf(format, argptr);

	printf("\n");
	fflush(stdout);
}

#define DebugLog(...) DebugLogImpl(__FUNCTION__, "INFO", __VA_ARGS__)
#define DebugLogWarning(...) DebugLogImpl(__FUNCTION__, "WARNING", __VA_ARGS__)
#define DebugLogError(...) DebugLogImpl(__FUNCTION__, "ERROR", __VA_ARGS__)
#define TraceLog(...) DebugLogImpl(__FUNCTION__, "TRACE", __VA_ARGS__)

#else

#define DebugLog(...)
#define DebugLogWarning(...)
#define DebugLogError(...)
#define TraceLog(...)

#endif
