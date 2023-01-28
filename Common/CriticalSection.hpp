#pragma once

#include "NtBase.hpp"

//
// A simple reentrant lock based on Windows Critical Sections.
//

class CriticalSection
{
	CriticalSection(const CriticalSection&) = delete;
	CriticalSection& operator=(const CriticalSection&) = delete;

protected:

	CRITICAL_SECTION m_cs;

public:

	CriticalSection()
	{
		(void) InitializeCriticalSection(&m_cs);
	}

	CriticalSection(DWORD spin_count)
	{
		(void) InitializeCriticalSectionAndSpinCount(&m_cs, spin_count);
	}

	~CriticalSection()
	{
		DeleteCriticalSection(&m_cs);
	}

	void Lock()
	{
		EnterCriticalSection(&m_cs);
	}

	bool TryLock()
	{
		return TryEnterCriticalSection(&m_cs) ? true : false;
	}

	bool TryLock(DWORD timeout)
	{
		ULONGLONG start = GetTickCount64();
		while (true)
		{
			if (TryLock())
			{
				return true;
			}
			if ((GetTickCount64() - start) >= timeout)
			{
				return false;
			}
			YieldProcessor();
		}
		return false;
	}

	void Unlock()
	{
		LeaveCriticalSection(&m_cs);
	}
};

template<typename T>
class ScopedLock
{
	ScopedLock() = delete;
	ScopedLock(const ScopedLock&) = delete;
	ScopedLock& operator=(const ScopedLock&) = delete;

protected:

	T& m_lock;

public:

	ScopedLock(T& lock) : m_lock(lock)
	{
		m_lock.Lock();
	}

	~ScopedLock()
	{
		m_lock.Unlock();
	}
};
