#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <new>
#include "Common/Win32.hpp"
#include <strsafe.h>
#include <objbase.h>

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
#define DebugLog(...) { printf("[%5d] ", GetThreadId(GetCurrentThread())); printf(__VA_ARGS__); printf("\n"); }
#define DebugLogError(...) { printf("[%5d] ", GetThreadId(GetCurrentThread())); printf("ERROR: "); printf(__VA_ARGS__); printf("\n"); }
#endif
