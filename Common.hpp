#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <new>
#include "Common/Win32.hpp"
#include <algorithm> // std::min and std::max.

template <class T> void SafeRelease(T*& com_obj_ptr)
{
	if (com_obj_ptr)
	{
		com_obj_ptr->Release();
		com_obj_ptr = nullptr;
	}
}

inline bool StringEquals(const char *l, const char *r)
{
	for (; *l == *r && *l; l++, r++);
	return (*(unsigned char *)l - *(unsigned char *)r) == 0;
}

inline bool StringEquals(const wchar_t *l, const wchar_t *r)
{
	for (; *l == *r && *l; l++, r++);
	return (*l - *r) == 0;
}

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
		printf("%04d/%02d/%02d\n", now.wYear, now.wMonth, now.wDay);
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
