#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <new>
#include "Common/Win32.hpp"
#include <strsafe.h>
#include <objbase.h>
#include <algorithm> // std::min and std::max.

template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}

#ifdef NDEBUG

#define DebugLog(...)
#define DebugLogError(...)

#else

inline void DebugLog(const char * format, ...)
{
	printf("[%5d] ", GetThreadId(GetCurrentThread()));

	va_list argptr;
	va_start(argptr, format);
	vprintf(format, argptr);

	printf("\n");
}

inline void DebugLogError(const char * format, ...)
{
	printf("[%5d] ", GetThreadId(GetCurrentThread()));
	printf("ERROR: ");

	va_list argptr;
	va_start(argptr, format);
	vprintf(format, argptr);

	printf("\n");
}

#endif
