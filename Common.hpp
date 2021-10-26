#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <new>
#include "Common/Win32.hpp"
#include <objbase.h>
#include <algorithm> // std::min and std::max.

template <class T> void SafeRelease(T*& com_obj_ptr)
{
	if (com_obj_ptr)
	{
		com_obj_ptr->Release();
		com_obj_ptr = nullptr;
	}
}

#ifdef NDEBUG

#define DebugLog(...)
#define DebugLogError(...)

#else

inline void DebugLog(const char * format, ...)
{
	SYSTEMTIME now = {0};
	GetSystemTime(&now);
	printf("%02d:%02d:%02d.%03d", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
	printf(" [%5d] ", GetThreadId(GetCurrentThread()));

	va_list argptr;
	va_start(argptr, format);
	vprintf(format, argptr);

	printf("\n");
}

#define DebugLogError(...) DebugLog("ERROR: " __VA_ARGS__)

#endif
