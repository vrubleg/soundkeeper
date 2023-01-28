#pragma once

#include "NtBase.hpp"
#include <initializer_list>

// ---------------------------------------------------------------------------------------------------------------------

class Handle
{
	Handle(const Handle&) = delete;
	Handle& operator= (const Handle&) = delete;

protected:

	HANDLE m_handle;

public:

	Handle(HANDLE handle) : m_handle(handle) {}
	HANDLE GetHandle() const { return m_handle; }
	operator HANDLE() const { return m_handle; }
	~Handle() { if (m_handle) { CloseHandle(m_handle); } }
};

inline DWORD WaitForOne(HANDLE handle, DWORD timeout = INFINITE)
{
	return WaitForSingleObject(handle, timeout);
}

inline DWORD WaitForAny(std::initializer_list<HANDLE> handles, DWORD timeout = INFINITE)
{
	return WaitForMultipleObjects((DWORD)handles.size(), handles.begin(), FALSE, timeout);
}

inline DWORD WaitForAll(std::initializer_list<HANDLE> handles, DWORD timeout = INFINITE)
{
	return WaitForMultipleObjects((DWORD)handles.size(), handles.begin(), TRUE, timeout);
}

// ---------------------------------------------------------------------------------------------------------------------
