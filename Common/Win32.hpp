#pragma once

#pragma warning(disable : 26812)

#define _WIN32_WINNT 0x0600
#include <sdkddkver.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOCOMM
#define NOKANJI
#define NOSOUND
#include <windows.h>

class Handle
{
	Handle (const Handle&) = delete;
	Handle& operator= (const Handle&) = delete;

protected:

	HANDLE m_handle;

public:

	Handle(HANDLE handle) : m_handle(handle) {}
	HANDLE GetHandle() const { return m_handle; }
	operator HANDLE() const { return m_handle; }
	~Handle() { if (m_handle) { CloseHandle(m_handle); } }
};

#include "NtEvent.hpp"
#include "CriticalSection.hpp"
