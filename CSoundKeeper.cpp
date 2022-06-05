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

#ifdef _CONSOLE

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

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR device_id)
{
	DebugLog("Device '%S' is default for flow %d and role %d.", device_id ? device_id : L"", flow, role);
	if (m_cfg_device_type == KeepDeviceType::Primary && flow == eRender && role == eConsole)
	{
		this->FireRestart();
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceAdded(LPCWSTR device_id)
{
	DebugLog("Device '%S' was added.", device_id);
	if (m_cfg_device_type != KeepDeviceType::Primary)
	{
		this->FireRestart();
	}
	return S_OK;
};

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceRemoved(LPCWSTR device_id)
{
	ScopedLock lock(m_mutex);
	DebugLog("Device '%S' was removed.", device_id);
	if (this->FindSession(device_id))
	{
		this->FireRestart();
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceStateChanged(LPCWSTR device_id, DWORD new_state)
{
	ScopedLock lock(m_mutex);
	DebugLog("Device '%S' new state: %d.", device_id, new_state);
	if (new_state == DEVICE_STATE_ACTIVE || this->FindSession(device_id))
	{
		this->FireRestart();
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnPropertyValueChanged(LPCWSTR device_id, const PROPERTYKEY key)
{
	return S_OK;
}

uint32_t GetDeviceFormFactor(IMMDevice* device)
{
	uint32_t formfactor = -1;

	IPropertyStore* properties = nullptr;
	HRESULT hr = device->OpenPropertyStore(STGM_READ, &properties);
	if (FAILED(hr))
	{
		DebugLogWarning("Unable to retrieve property store of an audio device: 0x%08X.", hr);
		return formfactor;
	}

	PROPVARIANT prop_formfactor;
	PropVariantInit(&prop_formfactor);
	hr = properties->GetValue(PKEY_AudioEndpoint_FormFactor, &prop_formfactor);
	if (SUCCEEDED(hr) && prop_formfactor.vt == VT_UI4)
	{
		formfactor = prop_formfactor.uintVal;
#ifdef _CONSOLE
		LPWSTR device_id = nullptr;
		hr = device->GetId(&device_id);
		if (FAILED(hr))
		{
			DebugLogWarning("Unable to get device ID: 0x%08X.", hr);
		}
		else
		{
			DebugLog("Device ID: '%S'. Form Factor: %d.", device_id, formfactor);
			CoTaskMemFree(device_id);
		}
#endif
	}
	else
	{
		DebugLogWarning("Unable to retrieve formfactor of an audio device: 0x%08X.", hr);
	}

	PropVariantClear(&prop_formfactor);
	SafeRelease(properties);

	return formfactor;
}

HRESULT CSoundKeeper::Start()
{
	ScopedLock lock(m_mutex);

	if (m_is_started) return S_OK;
	HRESULT hr = S_OK;
	m_is_retry_required = false;

	IMMDeviceCollection *dev_collection = NULL;

	if (m_cfg_device_type == KeepDeviceType::Primary)
	{
		DebugLog("Getting primary audio device...");

		IMMDevice* device;
		hr = m_dev_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
		if (FAILED(hr))
		{
			if (hr == E_NOTFOUND)
			{
				DebugLogWarning("No primary device found. Working as a dummy...");
				goto exit_started;
			}
			else
			{
				DebugLogError("Unable to retrieve default render device: 0x%08X.", hr);
				goto exit;
			}
		}

		uint32_t formfactor = GetDeviceFormFactor(device);

		if (formfactor == -1)
		{
			SafeRelease(device);
			goto exit_started;
		}
		else if (formfactor == RemoteNetworkDevice)
		{
			DebugLog("Ignoring remote desktop audio device.");
			SafeRelease(device);
			goto exit_started;
		}

		m_sessions_count = 1;
		m_sessions = new CKeepSession*[m_sessions_count]();

		m_sessions[0] = new CKeepSession(this, device);
		m_sessions[0]->SetStreamType(m_cfg_stream_type);
		m_sessions[0]->SetFrequency(m_cfg_frequency);
		m_sessions[0]->SetAmplitude(m_cfg_amplitude);
		m_sessions[0]->SetPeriodicPlaying(m_cfg_play_seconds);
		m_sessions[0]->SetPeriodicWaiting(m_cfg_wait_seconds);
		m_sessions[0]->SetFading(m_cfg_fade_seconds);

		if (!m_sessions[0]->Start())
		{
			m_is_retry_required = true;
		}

		SafeRelease(device);
	}
	else
	{
		DebugLog("Enumerating active audio devices...");

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

			uint32_t formfactor = GetDeviceFormFactor(device);

			if (formfactor == -1)
			{
				SafeRelease(device);
				continue;
			}
			else if (formfactor == RemoteNetworkDevice)
			{
				DebugLog("Ignoring remote desktop audio device.");
				SafeRelease(device);
				continue;
			}
			else if ((m_cfg_device_type == KeepDeviceType::Digital || m_cfg_device_type == KeepDeviceType::Analog)
				&& (m_cfg_device_type == KeepDeviceType::Digital) != (formfactor == SPDIF || formfactor == HDMI))
			{
				DebugLog("Skipping this device because of the Digital / Analog filter.");
				SafeRelease(device);
				continue;
			}

			m_sessions[i] = new CKeepSession(this, device);
			m_sessions[i]->SetStreamType(m_cfg_stream_type);
			m_sessions[i]->SetFrequency(m_cfg_frequency);
			m_sessions[i]->SetAmplitude(m_cfg_amplitude);
			m_sessions[i]->SetPeriodicPlaying(m_cfg_play_seconds);
			m_sessions[i]->SetPeriodicWaiting(m_cfg_wait_seconds);
			m_sessions[i]->SetFading(m_cfg_fade_seconds);

			if (!m_sessions[i]->Start())
			{
				m_is_retry_required = true;
			}

			SafeRelease(device);
		}

		if (m_sessions_count == 0)
		{
			DebugLogWarning("No suitable devices found. Working as a dummy...");
		}
	}

exit_started:

	m_is_started = true;

exit:

	SafeRelease(dev_collection);
	return hr;
}

bool CSoundKeeper::Retry()
{
	ScopedLock lock(m_mutex);

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
	ScopedLock lock(m_mutex);

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
	ScopedLock lock(m_mutex);

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

CKeepSession* CSoundKeeper::FindSession(LPCWSTR device_id)
{
	ScopedLock lock(m_mutex);

	for (UINT i = 0; i < m_sessions_count; i++)
	{
		if (m_sessions[i] == nullptr || !m_sessions[i]->IsValid())
		{
			continue;
		}

		if (StringEquals(m_sessions[i]->GetDeviceId(), device_id))
		{
			return m_sessions[i];
		}
	}

	return nullptr;
}

void CSoundKeeper::FireRetry()
{
	DebugLog("Fire Retry!");
	m_do_retry = true;
}

void CSoundKeeper::FireRestart()
{
	DebugLog("Fire Restart!");
	m_do_restart = true;
}

void CSoundKeeper::FireShutdown()
{
	DebugLog("Fire Shutdown!");
	m_do_shutdown = true;
}

void CSoundKeeper::ParseStreamArgs(KeepStreamType stream_type, const char* args)
{
	this->SetStreamType(stream_type);
	this->SetFrequency(1.00);
	this->SetAmplitude(0.01);
	this->SetFading(0.1);

	char* p = (char*) args;
	while (*p)
	{
		if (*p == ' ' || *p == '-') { p++; }
		else if (*p == 'f' || *p == 'a' || *p == 'l' || *p == 'w' || *p == 't')
		{
			char type = *p;
			p++;
			while (*p == ' ' || *p == '=') { p++; }
			double value = fabs(strtod(p, &p));
			if (type == 'f')
			{
				this->SetFrequency(std::min(value, 96000.0));
			}
			else if (type == 'a')
			{
				this->SetAmplitude(std::min(value / 100.0, 1.0));
			}
			else if (type == 'l')
			{
				this->SetPeriodicPlaying(value);
			}
			else if (type == 'w')
			{
				this->SetPeriodicWaiting(value);
			}
			else if (type == 't')
			{
				this->SetFading(value);
			}
		}
		else
		{
			break;
		}
	}
}

void CSoundKeeper::ParseModeString(const char* args)
{
	char buf[MAX_PATH];
	strcpy_s(buf, args);
	_strlwr(buf);

	if (strstr(buf, "all"))     { this->SetDeviceType(KeepDeviceType::All); }
	if (strstr(buf, "analog"))  { this->SetDeviceType(KeepDeviceType::Analog); }
	if (strstr(buf, "digital")) { this->SetDeviceType(KeepDeviceType::Digital); }
	if (strstr(buf, "kill"))    { this->SetDeviceType(KeepDeviceType::None); }

	if (strstr(buf, "zero") || strstr(buf, "null"))
	{
		this->SetStreamType(KeepStreamType::Zero);
	}
	else if (char* p = strstr(buf, "sine"))
	{
		this->ParseStreamArgs(KeepStreamType::Sine, p+4);
	}
	else if (char* p = strstr(buf, "white"))
	{
		this->ParseStreamArgs(KeepStreamType::WhiteNoise, p+5);
	}
	else if (char* p = strstr(buf, "brown"))
	{
		this->ParseStreamArgs(KeepStreamType::BrownNoise, p+5);
	}
}

HRESULT CSoundKeeper::Run()
{
	this->SetDeviceType(KeepDeviceType::Primary);
	this->SetStreamType(KeepStreamType::Fluctuate);

	// Parse file name for defaults.
	char fn_buffer[MAX_PATH];
	DWORD fn_size = GetModuleFileNameA(NULL, fn_buffer, MAX_PATH);
	if (fn_size != 0 && fn_size != MAX_PATH)
	{
		char* filename = strrchr(fn_buffer, '\\');
		if (filename)
		{
			filename++;
			DebugLog("Exe File Name: %s.", filename);
			this->ParseModeString(filename);
		}
	}

	// Parse command line for arguments.
	if (const char* cmdln = GetCommandLineA())
	{
		// Skip program file name.
		while (*cmdln == ' ') { cmdln++; }
		if (cmdln[0] == '"')
		{
			cmdln++;
			while (*cmdln != '"' && *cmdln != 0) { cmdln++; }
			if (*cmdln == '"') { cmdln++; }
		}
		else
		{
			while (*cmdln != ' ' && *cmdln != 0) { cmdln++; }
		}
		while (*cmdln == ' ') { cmdln++; }

		if (*cmdln != 0)
		{
			DebugLog("Command Line: %s.", cmdln);
			this->ParseModeString(cmdln);
		}
	}

#ifdef _CONSOLE

	switch (this->GetDeviceType())
	{
		case KeepDeviceType::None:      DebugLog("Device Type: None."); break;
		case KeepDeviceType::Primary:   DebugLog("Device Type: Primary."); break;
		case KeepDeviceType::All:       DebugLog("Device Type: All."); break;
		case KeepDeviceType::Analog:    DebugLog("Device Type: Analog."); break;
		case KeepDeviceType::Digital:   DebugLog("Device Type: Digital."); break;
		default:                        DebugLogError("Unknown Device Type."); break;
	}

	switch (this->GetStreamType())
	{
		case KeepStreamType::Zero:      DebugLog("Stream Type: Zero."); break;
		case KeepStreamType::Fluctuate: DebugLog("Stream Type: Fluctuate."); break;
		case KeepStreamType::Sine:      DebugLog("Stream Type: Sine (Frequency: %.3fHz; Amplitude: %.3f%%; Fading: %.3fs).", this->GetFrequency(), this->GetAmplitude() * 100.0, this->GetFading()); break;
		case KeepStreamType::WhiteNoise:DebugLog("Stream Type: White Noise (Amplitude: %.3f%%; Fading: %.3fs).", this->GetAmplitude() * 100.0, this->GetFading()); break;
		case KeepStreamType::BrownNoise:DebugLog("Stream Type: Brown Noise (Amplitude: %.3f%%; Fading: %.3fs).", this->GetAmplitude() * 100.0, this->GetFading()); break;
		default:                        DebugLogError("Unknown Stream Type."); break;
	}

	if (this->GetPeriodicPlaying() || this->GetPeriodicWaiting())
	{
		DebugLog("Periodicity: Enabled (Length: %.3fs; Waiting: %.3fs).", this->GetPeriodicPlaying(), this->GetPeriodicWaiting());
	}
	else
	{
		DebugLog("Periodicity: Disabled.");
	}

#endif

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

	HRESULT hr = S_OK;

	if (m_cfg_device_type == KeepDeviceType::None)
	{
		DebugLog("Self kill mode is enabled. Exit.");
		goto exit;
	}

	// Initialization.

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_dev_enumerator));
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

__forceinline HRESULT CSoundKeeper::Main()
{
	DebugLog("Main thread started.");

	if (HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE); FAILED(hr))
	{
#ifndef _CONSOLE
		MessageBoxA(0, "Cannot initialize COM.", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
#else
		DebugLogError("Cannot initialize COM: 0x%08X.", hr);
#endif
		return hr;
	}

	CSoundKeeper* keeper = new CSoundKeeper();
	HRESULT hr = keeper->Run();
	SafeRelease(keeper);

	CoUninitialize();

#ifndef _CONSOLE
	if (FAILED(hr))
	{
		MessageBoxA(0, "Cannot initialize WASAPI.", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
	}
#else
	if (hr == S_OK)
	{
		DebugLog("Main thread finished. Exit code: 0.", hr);
	}
	else
	{
		DebugLog("Main thread finished. Exit code: 0x%08X.", hr);
	}
#endif

	return hr;
}

#ifdef _CONSOLE

int main()
{
	return CSoundKeeper::Main();
}

#else

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
	return CSoundKeeper::Main();
}

#endif
