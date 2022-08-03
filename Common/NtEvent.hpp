#pragma once

#include "NtBase.hpp"

// ---------------------------------------------------------------------------------------------------------------------

EXTERN_C_START

typedef enum _EVENT_TYPE
{
	NotificationEvent,
	SynchronizationEvent
} EVENT_TYPE;

#ifndef EVENT_QUERY_STATE
#define EVENT_QUERY_STATE 0x0001
#endif

NTSYSCALLAPI
NTSTATUS
NTAPI
NtCreateEvent(
	_Out_ PHANDLE EventHandle,
	_In_ ACCESS_MASK DesiredAccess,
	_In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
	_In_ EVENT_TYPE EventType,
	_In_ BOOLEAN InitialState
);

NTSYSCALLAPI
NTSTATUS
NTAPI
NtSetEvent(
	_In_ HANDLE EventHandle,
	_Out_opt_ PLONG PreviousState
);

NTSYSCALLAPI
NTSTATUS
NTAPI
NtResetEvent(
	_In_ HANDLE EventHandle,
	_Out_opt_ PLONG PreviousState
);

NTSYSCALLAPI
NTSTATUS
NTAPI
NtPulseEvent(
	_In_ HANDLE EventHandle,
	_Out_opt_ PLONG PreviousState
);

typedef enum _EVENT_INFORMATION_CLASS
{
	EventBasicInformation
} EVENT_INFORMATION_CLASS;

typedef struct _EVENT_BASIC_INFORMATION
{
	EVENT_TYPE EventType;
	LONG EventState;
} EVENT_BASIC_INFORMATION, *PEVENT_BASIC_INFORMATION;

NTSYSCALLAPI
NTSTATUS
NTAPI
NtQueryEvent(
	_In_ HANDLE EventHandle,
	_In_ EVENT_INFORMATION_CLASS EventInformationClass,
	_Out_writes_bytes_(EventInformationLength) PVOID EventInformation,
	_In_ ULONG EventInformationLength,
	_Out_opt_ PULONG ReturnLength
);

EXTERN_C_END

// ---------------------------------------------------------------------------------------------------------------------

class SyncEvent : public Handle
{
public:

	enum class ResetMode { Manual, Auto };

	explicit SyncEvent(const ResetMode mode, const bool state) : Handle(NULL)
	{
		NtCreateEvent(
			&m_handle,
			EVENT_ALL_ACCESS,
			nullptr,
			(mode == ResetMode::Manual ? NotificationEvent : SynchronizationEvent),
			state
		);
	}

	bool Get() const
	{
		EVENT_BASIC_INFORMATION info {};
		NtQueryEvent(m_handle, EventBasicInformation, &info, sizeof(info), nullptr);
		return info.EventState;
	}

	void Set(const bool state)
	{
		if (state)
		{
			NtSetEvent(m_handle, nullptr);
		}
		else
		{
			NtResetEvent(m_handle, nullptr);
		}
	}

	bool GetSet(const bool state)
	{
		LONG prev = 0;
		if (state)
		{
			NtSetEvent(m_handle, &prev);
		}
		else
		{
			NtResetEvent(m_handle, &prev);
		}
		return prev;
	}

	void Pulse()
	{
		NtPulseEvent(m_handle, nullptr);
	}

	bool GetPulse()
	{
		LONG prev = 0;
		NtPulseEvent(m_handle, &prev);
		return prev;
	}
};

// ---------------------------------------------------------------------------------------------------------------------

class AutoResetEvent : public SyncEvent
{
public:

	AutoResetEvent(const bool state) : SyncEvent(ResetMode::Auto, state) {}

	AutoResetEvent& operator=(const bool state)
	{
		this->Set(state);
		return *this;
	}

	operator bool() const
	{
		return this->Get();
	}
};

// ---------------------------------------------------------------------------------------------------------------------

class ManualResetEvent : public SyncEvent
{
public:

	ManualResetEvent(const bool state) : SyncEvent(ResetMode::Manual, state) {}

	ManualResetEvent& operator=(const bool state)
	{
		this->Set(state);
		return *this;
	}

	operator bool() const
	{
		return this->Get();
	}
};

// ---------------------------------------------------------------------------------------------------------------------
