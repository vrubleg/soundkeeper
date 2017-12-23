#pragma once

#define _WIN32_WINNT 0x0600

#include <new>
#include <windows.h>
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
#define DebugErrorBox(...)
#else
#define DebugErrorBox(...) { char __msgbuf[8192]; sprintf_s(__msgbuf, __VA_ARGS__); MessageBoxA(0, __msgbuf, "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL); }
#endif
