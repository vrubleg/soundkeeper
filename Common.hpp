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

#ifdef _DEBUG

inline void DebugLog(const char * format, ...)
{
	static CriticalSection mutex;
	ScopedLock lock(mutex);

	SYSTEMTIME now = {0};
	GetSystemTime(&now);
	printf("%02d:%02d:%02d.%03d", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
	printf(" [%5d] ", GetThreadId(GetCurrentThread()));

	va_list argptr;
	va_start(argptr, format);
	vprintf(format, argptr);

	printf("\n");
}

#define DebugLogWarning(...) DebugLog("WARNING: " __VA_ARGS__)
#define DebugLogError(...) DebugLog("ERROR: " __VA_ARGS__)

#else

#define DebugLog(...)
#define DebugLogWarning(...)
#define DebugLogError(...)

#endif
