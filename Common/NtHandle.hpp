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

inline DWORD AlertableSleep(DWORD timeout)
{
	return SleepEx(timeout, TRUE);
}

inline DWORD WaitForOne(HANDLE handle, DWORD timeout = INFINITE)
{
	return WaitForSingleObject(handle, timeout);
}

inline DWORD AlertableWaitForOne(HANDLE handle, DWORD timeout = INFINITE)
{
	return WaitForSingleObjectEx(handle, timeout, TRUE);
}

inline DWORD WaitForAny(std::initializer_list<HANDLE> handles, DWORD timeout = INFINITE)
{
	return WaitForMultipleObjects((DWORD)handles.size(), handles.begin(), FALSE, timeout);
}

inline DWORD AlertableWaitForAny(std::initializer_list<HANDLE> handles, DWORD timeout = INFINITE)
{
	return WaitForMultipleObjectsEx((DWORD)handles.size(), handles.begin(), FALSE, timeout, TRUE);
}

inline DWORD WaitForAll(std::initializer_list<HANDLE> handles, DWORD timeout = INFINITE)
{
	return WaitForMultipleObjects((DWORD)handles.size(), handles.begin(), TRUE, timeout);
}

inline DWORD AlertableWaitForAll(std::initializer_list<HANDLE> handles, DWORD timeout = INFINITE)
{
	return WaitForMultipleObjectsEx((DWORD)handles.size(), handles.begin(), TRUE, timeout, TRUE);
}

inline DWORD WaitForOneOrMsg(HANDLE handle, DWORD timeout = INFINITE)
{
	return MsgWaitForMultipleObjectsEx(1, &handle, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

inline DWORD AlertableWaitForOneOrMsg(HANDLE handle, DWORD timeout = INFINITE)
{
	return MsgWaitForMultipleObjectsEx(1, &handle, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE | MWMO_ALERTABLE);
}

inline DWORD WaitForAnyOrMsg(std::initializer_list<HANDLE> handles, DWORD timeout = INFINITE)
{
	return MsgWaitForMultipleObjectsEx((DWORD)handles.size(), handles.begin(), timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

inline DWORD AlertableWaitForAnyOrMsg(std::initializer_list<HANDLE> handles, DWORD timeout = INFINITE)
{
	return MsgWaitForMultipleObjectsEx((DWORD)handles.size(), handles.begin(), timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE | MWMO_ALERTABLE);
}

inline DWORD WaitForAllAndMsg(std::initializer_list<HANDLE> handles, DWORD timeout = INFINITE)
{
	return MsgWaitForMultipleObjectsEx((DWORD)handles.size(), handles.begin(), timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE | MWMO_WAITALL);
}

inline DWORD AlertableWaitForAllAndMsg(std::initializer_list<HANDLE> handles, DWORD timeout = INFINITE)
{
	return MsgWaitForMultipleObjectsEx((DWORD)handles.size(), handles.begin(), timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE | MWMO_ALERTABLE | MWMO_WAITALL);
}

// ---------------------------------------------------------------------------------------------------------------------
