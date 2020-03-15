#pragma once

#include <mmdeviceapi.h>
#include <audiopolicy.h>

class CSoundKeeper;
#include "CKeepSession.hpp"

class CSoundKeeper : public IMMNotificationClient
{
protected:

	LONG m_ref_count = 1;

	~CSoundKeeper();

public:

	CSoundKeeper();

	// IUnknown methods

	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface);

	// Callback methods for device-event notifications.

	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId);
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId);
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId);
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState);
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key);

protected:

	IMMDeviceEnumerator*    m_dev_enumerator = nullptr;
	bool                    m_is_started = false;
	bool                    m_is_retry_required = false;
	CKeepSession**          m_sessions = nullptr;
	UINT                    m_sessions_count = 0;
	HANDLE                  m_shutdown_event = NULL;
	HANDLE                  m_restart_event = NULL;

	HRESULT Start();
	HRESULT Stop();
	HRESULT Restart();
	bool Retry();

public:

	void FireRestart();
	void FireShutdown();
	HRESULT Main();
};
