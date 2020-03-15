#include "StdAfx.h"
#include "CSoundKeeper.hpp"

CSoundKeeper::CSoundKeeper() { }
CSoundKeeper::~CSoundKeeper() { }

// IUnknown methods

ULONG STDMETHODCALLTYPE CSoundKeeper::AddRef()
{
	return InterlockedIncrement(&m_ref_count);
}

ULONG STDMETHODCALLTYPE CSoundKeeper::Release()
{
	ULONG result = InterlockedDecrement(&m_ref_count);
	if (result == 0)
	{
		delete this;
	}
	return result;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::QueryInterface(REFIID riid, VOID **ppvInterface)
{
	if (IID_IUnknown == riid)
	{
		AddRef();
		*ppvInterface = (IUnknown*)this;
	}
	else if (__uuidof(IMMNotificationClient) == riid)
	{
		AddRef();
		*ppvInterface = (IMMNotificationClient*)this;
	}
	else
	{
		*ppvInterface = NULL;
		return E_NOINTERFACE;
	}
	return S_OK;
}

// Callback methods for device-event notifications.

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceAdded(LPCWSTR pwstrDeviceId)
{
	FireRestart();
	return S_OK;
};

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
	FireRestart();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState)
{
	FireRestart();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key)
{
	return S_OK;
}

HRESULT CSoundKeeper::Start()
{
	if (m_is_started) return S_OK;
	HRESULT hr = S_OK;
	m_is_retry_required = false;

	IMMDeviceCollection *DevCollection = NULL;
	hr = m_dev_enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &DevCollection);
	if (FAILED(hr))
	{
		DebugLogError("Unable to retrieve device collection: 0x%08X.", hr);
		goto exit;
	}

	hr = DevCollection->GetCount(&m_sessions_count);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get device collection length: 0x%08X.", hr);
		goto exit;
	}

	m_sessions = new CKeepSession*[m_sessions_count]();

	for (UINT i = 0; i < m_sessions_count; i++)
	{
		m_sessions[i] = nullptr;

		IMMDevice *device;
		DevCollection->Item(i, &device);
		IPropertyStore *properties;
		hr = device->OpenPropertyStore(STGM_READ, &properties);

		PROPVARIANT formfactor;
		PropVariantInit(&formfactor);
		hr = properties->GetValue(PKEY_AudioEndpoint_FormFactor, &formfactor);
		SafeRelease(&properties);
		if (FAILED(hr) || formfactor.vt != VT_UI4 || formfactor.uintVal != SPDIF && formfactor.uintVal != HDMI)
		{
			PropVariantClear(&formfactor);
			SafeRelease(&device);
			continue;
		}
		PropVariantClear(&formfactor);

		m_sessions[i] = new CKeepSession(this, device);
		SafeRelease(&device);

		if (!m_sessions[i]->Start())
		{
			m_is_retry_required = true;
		}
	}

	m_is_started = true;

exit:

	SafeRelease(&DevCollection);
	return hr;
}

bool CSoundKeeper::Retry()
{
	if (!m_is_retry_required) return true;
	m_is_retry_required = false;

	if (m_sessions != nullptr)
	{
		for (UINT i = 0; i < m_sessions_count; i++)
		{
			if (m_sessions[i] != nullptr)
			{
				if (!m_sessions[i]->Start())
				{
					m_is_retry_required = true;
				}
			}
		}
	}

	return !m_is_retry_required;
}

HRESULT CSoundKeeper::Stop()
{
	if (!m_is_started) return S_OK;

	if (m_sessions != nullptr)
	{
		for (UINT i = 0; i < m_sessions_count; i++)
		{
			if (m_sessions[i] != nullptr)
			{
				m_sessions[i]->Stop();
				m_sessions[i]->Release();
			}
		}
		delete m_sessions;
	}

	m_sessions = nullptr;
	m_sessions_count = 0;
	m_is_started = false;
	return S_OK;
}

HRESULT CSoundKeeper::Restart()
{
	HRESULT hr = S_OK;

	hr = this->Stop();
	if (FAILED(hr))
	{
		return hr;
	}

	hr = this->Start();
	if (FAILED(hr))
	{
		return hr;
	}

	return S_OK;
}

void CSoundKeeper::FireRetry()
{
	SetEvent(m_retry_event);
}

void CSoundKeeper::FireRestart()
{
	SetEvent(m_restart_event);
}

void CSoundKeeper::FireShutdown()
{
	SetEvent(m_shutdown_event);
}

HRESULT CSoundKeeper::Main()
{
	HRESULT hr = S_OK;

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_dev_enumerator));
	if (FAILED(hr))
	{
		DebugLogError("Unable to instantiate device enumerator: 0x%08X.", hr);
		goto exit;
	}

	m_shutdown_event = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	if (m_shutdown_event == NULL)
	{
		DebugLogError("Unable to create shutdown event: 0x%08X.", GetLastError());
		hr = E_FAIL;
		goto exit;
	}

	m_restart_event = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	if (m_restart_event == NULL)
	{
		DebugLogError("Unable to create restart event: 0x%08X.", GetLastError());
		hr = E_FAIL;
		goto exit;
	}

	m_retry_event = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	if (m_retry_event == NULL)
	{
		DebugLogError("Unable to create retry event: 0x%08X.", GetLastError());
		hr = E_FAIL;
		goto exit;
	}

	hr = m_dev_enumerator->RegisterEndpointNotificationCallback(this);
	if (FAILED(hr))
	{
		DebugLogError("Unable to register for stream switch notifications: 0x%08X.", hr);
		goto exit;
	}

	// Main loop
	DebugLog("Start");
	this->Start();
	HANDLE wait[] = { m_retry_event, m_restart_event, m_shutdown_event };
	bool working = true;
	while (working)
	{
		switch (WaitForMultipleObjects(_countof(wait), wait, FALSE, m_is_retry_required ? 500 : INFINITE))
		{
		case WAIT_OBJECT_0 + 0:

			// Prevent multiple retries.
			while (WaitForSingleObject(m_retry_event, 500) != WAIT_TIMEOUT)
			{
				Sleep(500);
			}
			m_is_retry_required = true;

		case WAIT_TIMEOUT:

			DebugLog("Retry");
			this->Retry();
			break;

		case WAIT_OBJECT_0 + 1:

			// Prevent multiple restarts.
			while (WaitForSingleObject(m_restart_event, 500) != WAIT_TIMEOUT)
			{
				Sleep(500);
			}

			DebugLog("Restart");
			this->Restart();
			break;

		case WAIT_OBJECT_0 + 2:
		default:

			DebugLog("Shutdown");
			// Shutdown
			working = false; // We're done, exit the loop
			break;
		}
	}
	Stop();

exit:

	if (m_shutdown_event)
	{
		CloseHandle(m_shutdown_event);
		m_shutdown_event = NULL;
	}
	if (m_restart_event)
	{
		CloseHandle(m_restart_event);
		m_restart_event = NULL;
	}
	if (m_retry_event)
	{
		CloseHandle(m_retry_event);
		m_retry_event = NULL;
	}
	if (m_dev_enumerator)
	{
		m_dev_enumerator->UnregisterEndpointNotificationCallback(this);
	}
	SafeRelease(&m_dev_enumerator);

	return hr;
}
