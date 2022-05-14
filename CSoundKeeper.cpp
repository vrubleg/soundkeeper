#include "Common.hpp"
#include "CSoundKeeper.hpp"

//
// Get time to sleeping (in seconds). It is not precise and updated just once in 15 seconds!
//

#include <powrprof.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
#endif

// On Windows 7-10, it outputs -1 when auto sleep mode is disabled.
// On Windows 10, it may output negative values (-30, -60, ...) when sleeping was postponed not by user input.
// On Windows 11, the TimeRemaining field is always 0 for some reason.
LONG GetSecondsToSleeping()
{
	struct SYSTEM_POWER_INFORMATION {
		ULONG MaxIdlenessAllowed;
		ULONG Idleness;
		LONG TimeRemaining;
		UCHAR CoolingMode;
	} spi = {0};

	if (!NT_SUCCESS(CallNtPowerInformation(SystemPowerInformation, NULL, 0, &spi, sizeof(spi))))
	{
		DebugLogError("Cannot get remaining time to sleeping.");
		return 0;
	}

#ifdef _DEBUG

	static LONG last_result = 0x80000000;

	if (last_result != spi.TimeRemaining)
	{
		last_result = spi.TimeRemaining;
		DebugLog("Remaining time to sleeping: %d seconds.", spi.TimeRemaining);
	}

#endif

	return spi.TimeRemaining;
}

static bool g_is_buggy_powerinfo = []()
{
	bool result = (GetSecondsToSleeping() == 0);
	if (result)
	{
		DebugLogWarning("Buggy power information.");
	}
	return result;
}();

//
// CSoundKeeper implementation.
//

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
	if (m_cfg_device_type == KeepDeviceType::Primary) { FireRestart(); }
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceAdded(LPCWSTR pwstrDeviceId)
{
	DebugLog("Device '%S' was added.", pwstrDeviceId);
	FireRestart();
	return S_OK;
};

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
	DebugLog("Device '%S' was removed.", pwstrDeviceId);
	FireRestart();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState)
{
	DebugLog("Device '%S' new state: %d.", pwstrDeviceId, dwNewState);
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

	IMMDeviceCollection *dev_collection = NULL;

	if (m_cfg_device_type == KeepDeviceType::Primary)
	{
		IMMDevice* device;
		hr = m_dev_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
		if (FAILED(hr))
		{
			DebugLogError("Unable to retrieve default render device: 0x%08X.", hr);
			goto exit;
		}
		m_sessions_count = 1;
		m_sessions = new CKeepSession*[m_sessions_count]();

		m_sessions[0] = new CKeepSession(this, device);
		m_sessions[0]->SetStreamType(m_cfg_stream_type);
		m_sessions[0]->SetFrequency(m_cfg_frequency);
		m_sessions[0]->SetAmplitude(m_cfg_amplitude);

		if (!m_sessions[0]->Start())
		{
			m_is_retry_required = true;
		}

		SafeRelease(device);
	}
	else
	{
		hr = m_dev_enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &dev_collection);
		if (FAILED(hr))
		{
			DebugLogError("Unable to retrieve device collection: 0x%08X.", hr);
			goto exit;
		}

		hr = dev_collection->GetCount(&m_sessions_count);
		if (FAILED(hr))
		{
			DebugLogError("Unable to get device collection length: 0x%08X.", hr);
			goto exit;
		}

		m_sessions = new CKeepSession*[m_sessions_count]();

		for (UINT i = 0; i < m_sessions_count; i++)
		{
			m_sessions[i] = nullptr;

			IMMDevice* device;
			dev_collection->Item(i, &device);

			if (m_cfg_device_type != KeepDeviceType::All)
			{
				IPropertyStore* properties = nullptr;
				hr = device->OpenPropertyStore(STGM_READ, &properties);
				if (FAILED(hr))
				{
					continue;
				}

				PROPVARIANT formfactor;
				PropVariantInit(&formfactor);
				hr = properties->GetValue(PKEY_AudioEndpoint_FormFactor, &formfactor);
				SafeRelease(properties);
				if (FAILED(hr) || formfactor.vt != VT_UI4 || (m_cfg_device_type == KeepDeviceType::Digital) != (formfactor.uintVal == SPDIF || formfactor.uintVal == HDMI))
				{
					PropVariantClear(&formfactor);
					SafeRelease(device);
					continue;
				}
				PropVariantClear(&formfactor);
			}

			m_sessions[i] = new CKeepSession(this, device);
			m_sessions[i]->SetStreamType(m_cfg_stream_type);
			m_sessions[i]->SetFrequency(m_cfg_frequency);
			m_sessions[i]->SetAmplitude(m_cfg_amplitude);

			if (!m_sessions[i]->Start())
			{
				m_is_retry_required = true;
			}

			SafeRelease(device);
		}
	}

	m_is_started = true;

exit:

	SafeRelease(dev_collection);
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
	m_is_retry_required = false;
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
	m_do_retry = true;
}

void CSoundKeeper::FireRestart()
{
	m_do_restart = true;
}

void CSoundKeeper::FireShutdown()
{
	m_do_shutdown = true;
}

HRESULT CSoundKeeper::Main()
{
	// Stop another instance.

	Handle global_stop_event = CreateEventA(NULL, TRUE, FALSE, "SoundKeeperStopEvent");
	if (!global_stop_event)
	{
		DWORD le = GetLastError();
		DebugLogError("Unable to open global stop event. Error code: %d.", le);
		return HRESULT_FROM_WIN32(le);
	}

	Handle global_mutex = CreateMutexA(NULL, TRUE, "SoundKeeperMutex");
	if (!global_mutex)
	{
		DWORD le = GetLastError();
		DebugLogError("Unable to open global mutex. Error code: %d.", le);
		return HRESULT_FROM_WIN32(le);
	}

	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		DebugLog("Stopping another instance...");
		SetEvent(global_stop_event);
		bool is_timeout = WaitForOne(global_mutex, 1000) == WAIT_TIMEOUT;
		ResetEvent(global_stop_event);
		if (is_timeout)
		{
			DebugLogError("Time out.");
			return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
		}
	}

	// Initialization.

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_dev_enumerator));
	if (FAILED(hr))
	{
		DebugLogError("Unable to instantiate device enumerator: 0x%08X.", hr);
		goto exit;
	}

	hr = m_dev_enumerator->RegisterEndpointNotificationCallback(this);
	if (FAILED(hr))
	{
		DebugLogError("Unable to register for stream switch notifications: 0x%08X.", hr);
		goto exit;
	}

	// Working loop.

	DebugLog("Main loop started.");

	for (bool working = true; working; )
	{
		LONG seconds_to_sleeping = (g_is_buggy_powerinfo ? -1 : GetSecondsToSleeping());

		if (seconds_to_sleeping != -1 && seconds_to_sleeping <= 0)
		{
			if (m_is_started)
			{
				DebugLog("Going to sleep...");
				this->Stop();
			}
		}
		else
		{
			if (!m_is_started)
			{
				DebugLog("Starting...");
				this->Start();
			}
			else if (m_is_retry_required)
			{
				DebugLog("Retrying...");
				this->Retry();
			}
		}

		DWORD timeout = (seconds_to_sleeping != -1 && seconds_to_sleeping <= 30 || m_is_retry_required) ? 500 : 5000;

		switch (WaitForAny({ m_do_retry, m_do_restart, m_do_shutdown, global_stop_event }, timeout))
		{
		case WAIT_TIMEOUT:

			break;

		case WAIT_OBJECT_0 + 0:

			// Prevent multiple retries.
			while (WaitForOne(m_do_retry, 500) != WAIT_TIMEOUT)
			{
				Sleep(500);
			}
			m_is_retry_required = true;
			break;

		case WAIT_OBJECT_0 + 1:

			// Prevent multiple restarts.
			while (WaitForOne(m_do_restart, 500) != WAIT_TIMEOUT)
			{
				Sleep(500);
			}

			DebugLog("Restarting...");
			this->Restart();
			break;

		case WAIT_OBJECT_0 + 2:
		case WAIT_OBJECT_0 + 3:
		default:

			DebugLog("Shutdown.");
			working = false; // We're done, exit the loop
			break;
		}
	}

	Stop();

exit:

	if (m_dev_enumerator)
	{
		m_dev_enumerator->UnregisterEndpointNotificationCallback(this);
	}
	SafeRelease(m_dev_enumerator);
	ReleaseMutex(global_mutex);

	return hr;
}
