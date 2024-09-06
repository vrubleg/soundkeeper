// Tell mmdeviceapi.h to define its GUIDs in this translation unit.
#define INITGUID

#include "CSoundKeeper.hpp"

//
// Get time to sleeping (in seconds). It is not precise and updated just once in 15-30 seconds!
// On Windows 7-10, it outputs -1 (or values like -16, -31, ...) when auto sleep mode is disabled.
// On Windows 10, it may output negative values (-30, -60, ...) when sleeping was postponed not by user input.
// On Windows 11, the TimeRemaining field is always 0, so it doesn't work for some reason.
//

EXTERN_C NTSYSCALLAPI NTSTATUS NTAPI NtPowerInformation(
	_In_ POWER_INFORMATION_LEVEL InformationLevel,
	_In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
	_In_ ULONG InputBufferLength,
	_Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
	_In_ ULONG OutputBufferLength
);

uint32_t GetSecondsToSleeping()
{
	struct SYSTEM_POWER_INFORMATION {
		ULONG MaxIdlenessAllowed;
		ULONG Idleness;
		LONG TimeRemaining;
		UCHAR CoolingMode;
	} spi = {0};

	if (!NT_SUCCESS(NtPowerInformation(SystemPowerInformation, NULL, 0, &spi, sizeof(spi))))
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

	if (spi.TimeRemaining >= 0)
	{
		return spi.TimeRemaining;
	}

	return (spi.TimeRemaining % 5 == 0 ? 0 : -1);
}

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
// WARNING: Don't use m_mutex, it may cause a deadlock when CSoundKeeper::Restart -> Stop is in progress.

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
	DebugLog("Device '%S' was removed.", device_id);
	if (m_cfg_device_type != KeepDeviceType::Primary)
	{
		this->FireRestart();
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceStateChanged(LPCWSTR device_id, DWORD new_state)
{
	DebugLog("Device '%S' new state: %d.", device_id, new_state);
	if (new_state == DEVICE_STATE_ACTIVE)
	{
		this->FireRestart();
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnPropertyValueChanged(LPCWSTR device_id, const PROPERTYKEY key)
{
	return S_OK;
}

// Main thread methods.

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

	HRESULT hr = S_OK;
	if (m_is_started) { return hr; }
	m_is_retry_required = false;

	if (m_cfg_device_type == KeepDeviceType::Primary)
	{
		DebugLog("Getting primary audio device...");

		IMMDevice* device = nullptr;
		hr = m_dev_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
		if (FAILED(hr))
		{
			if (hr == E_NOTFOUND)
			{
				DebugLogWarning("No primary device found. Working as a dummy...");
				m_is_started = true;
				return hr;
			}
			else
			{
				DebugLogError("Unable to retrieve default render device: 0x%08X.", hr);
				return hr;
			}
		}
		defer [&] { device->Release(); };

		m_is_started = true;

		if (uint32_t formfactor = GetDeviceFormFactor(device); formfactor == -1)
		{
			return hr;
		}
		else if (!m_cfg_allow_remote && formfactor == RemoteNetworkDevice)
		{
			DebugLog("Ignoring remote desktop audio device.");
			return hr;
		}

		m_sessions_count = 1;
		m_sessions = new CSoundSession*[m_sessions_count]();

		m_sessions[0] = new CSoundSession(this, device);
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
	}
	else
	{
		DebugLog("Enumerating active audio devices...");

		IMMDeviceCollection* dev_collection = nullptr;
		hr = m_dev_enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &dev_collection);
		if (FAILED(hr))
		{
			DebugLogError("Unable to retrieve device collection: 0x%08X.", hr);
			return hr;
		}
		defer [&] { dev_collection->Release(); };

		hr = dev_collection->GetCount(&m_sessions_count);
		if (FAILED(hr))
		{
			DebugLogError("Unable to get device collection length: 0x%08X.", hr);
			return hr;
		}

		m_is_started = true;

		m_sessions = new CSoundSession*[m_sessions_count]();

		for (UINT i = 0; i < m_sessions_count; i++)
		{
			m_sessions[i] = nullptr;

			IMMDevice* device = nullptr;
			if (dev_collection->Item(i, &device) != S_OK) { continue; }
			defer [&] { device->Release(); };

			if (uint32_t formfactor = GetDeviceFormFactor(device); formfactor == -1)
			{
				continue;
			}
			else if (!m_cfg_allow_remote && formfactor == RemoteNetworkDevice)
			{
				DebugLog("Ignoring remote desktop audio device.");
				continue;
			}
			else if ((m_cfg_device_type == KeepDeviceType::Digital || m_cfg_device_type == KeepDeviceType::Analog)
				&& (m_cfg_device_type == KeepDeviceType::Digital) != (formfactor == SPDIF || formfactor == HDMI))
			{
				DebugLog("Skipping this device because of the Digital / Analog filter.");
				continue;
			}

			m_sessions[i] = new CSoundSession(this, device);
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
		}

		if (m_sessions_count == 0)
		{
			DebugLogWarning("No suitable devices found. Working as a dummy...");
		}
	}

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

CSoundSession* CSoundKeeper::FindSession(LPCWSTR device_id)
{
	ScopedLock lock(m_mutex);

	for (UINT i = 0; i < m_sessions_count; i++)
	{
		if (m_sessions[i] == nullptr)
		{
			continue;
		}

		if (auto curr = m_sessions[i]->GetDeviceId(); curr && StringEquals(curr, device_id))
		{
			// Call AddRef()? Use ComPtr?
			return m_sessions[i];
		}
	}

	return nullptr;
}

// Fire main thread control events.

void CSoundKeeper::FireRetry()
{
	TraceLog("Fire Retry!");
	m_do_retry = true;
}

void CSoundKeeper::FireRestart()
{
	TraceLog("Fire Restart!");
	m_do_restart = true;
}

void CSoundKeeper::FireShutdown()
{
	TraceLog("Fire Shutdown!");
	m_do_shutdown = true;
}

// Entry point methods.

void CSoundKeeper::SetStreamTypeDefaults(KeepStreamType stream_type)
{
	m_cfg_stream_type = stream_type;

	m_cfg_frequency = 0.0;
	m_cfg_amplitude = 0.0;
	m_cfg_play_seconds = 0.0;
	m_cfg_wait_seconds = 0.0;
	m_cfg_fade_seconds = 0.0;

	switch (stream_type)
	{
		case KeepStreamType::Fluctuate:
			m_cfg_frequency = 50.0;
			break;
		case KeepStreamType::Sine:
			m_cfg_frequency = 1.0;
			[[fallthrough]];
		case KeepStreamType::WhiteNoise:
		case KeepStreamType::BrownNoise:
		case KeepStreamType::PinkNoise:
			m_cfg_amplitude = 0.01;
			m_cfg_fade_seconds = 0.1;
			[[fallthrough]];
		default:
			break;
	}
}

void CSoundKeeper::ParseStreamArgs(KeepStreamType stream_type, const char* args)
{
	this->SetStreamTypeDefaults(stream_type);

	char* p = (char*) args;
	while (*p)
	{
		if (*p == ' ' || *p == '\t' || *p == '-') { p++; }
		else if (*p == 'f' || *p == 'a' || *p == 'l' || *p == 'w' || *p == 't')
		{
			char type = *p;
			p++;
			while (*p == ' ' || *p == '\t' || *p == '=') { p++; }
			if (*p < '0' || '9' < *p) { break; }

			double value = strtod(p, &p);
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
	if (strstr(buf, "remote"))  { this->SetAllowRemote(true); }
	if (strstr(buf, "nosleep")) { this->SetNoSleep(true); }

	if (strstr(buf, "openonly"))
	{
		this->SetStreamType(KeepStreamType::None);
	}
	else if (strstr(buf, "zero") || strstr(buf, "null"))
	{
		this->SetStreamType(KeepStreamType::Zero);
	}
	else if (char* p = strstr(buf, "fluctuate"))
	{
		this->ParseStreamArgs(KeepStreamType::Fluctuate, p+9);
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
	else if (char* p = strstr(buf, "pink"))
	{
		this->ParseStreamArgs(KeepStreamType::PinkNoise, p+4);
	}
}

HRESULT CSoundKeeper::Run()
{
	// Windows 8-10 audio service leaks handles and shared memory when exclusive mode is used, enable a workaround.
	uint32_t nt_build_number = GetNtBuildNumber();
	bool is_leaky_wasapi = 7601 < nt_build_number && nt_build_number < 22000;
	DebugLog("Windows Build Number: %u%s.", nt_build_number, is_leaky_wasapi ? " (leaky WASAPI)" : "");
	CSoundSession::EnableWaitExclusiveWorkaround(is_leaky_wasapi);

	// Set defaults.
	this->SetDeviceType(KeepDeviceType::Primary);
	this->SetStreamTypeDefaults(KeepStreamType::Fluctuate);

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

	if (GetSecondsToSleeping() == 0)
	{
		DebugLogWarning("Sleep timer informarion is not available. Sleep detection is disabled.");
		m_cfg_no_sleep = true;
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
		case KeepStreamType::None:      DebugLog("Stream Type: None (Open Only)."); break;
		case KeepStreamType::Zero:      DebugLog("Stream Type: Zero."); break;
		case KeepStreamType::Fluctuate: DebugLog("Stream Type: Fluctuate (Frequency: %.3fHz).", this->GetFrequency()); break;
		case KeepStreamType::Sine:      DebugLog("Stream Type: Sine (Frequency: %.3fHz; Amplitude: %.3f%%; Fading: %.3fs).", this->GetFrequency(), this->GetAmplitude() * 100.0, this->GetFading()); break;
		case KeepStreamType::WhiteNoise:DebugLog("Stream Type: White Noise (Amplitude: %.3f%%; Fading: %.3fs).", this->GetAmplitude() * 100.0, this->GetFading()); break;
		case KeepStreamType::BrownNoise:DebugLog("Stream Type: Brown Noise (Amplitude: %.3f%%; Fading: %.3fs).", this->GetAmplitude() * 100.0, this->GetFading()); break;
		case KeepStreamType::PinkNoise: DebugLog("Stream Type: Pink Noise (Amplitude: %.3f%%; Fading: %.3fs).", this->GetAmplitude() * 100.0, this->GetFading()); break;
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

	if (m_cfg_no_sleep)
	{
		DebugLog("Sleep Detection: Disabled.");
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
	defer [&] { ReleaseMutex(global_mutex); };

	HRESULT hr = S_OK;

	if (m_cfg_device_type == KeepDeviceType::None)
	{
		DebugLog("Self kill mode is enabled. Exit.");
		return hr;
	}

	// Initialization.

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_dev_enumerator));
	if (FAILED(hr))
	{
		DebugLogError("Unable to instantiate device enumerator: 0x%08X.", hr);
		return hr;
	}
	defer [&] { m_dev_enumerator->Release(); };

	hr = m_dev_enumerator->RegisterEndpointNotificationCallback(this);
	if (FAILED(hr))
	{
		DebugLogError("Unable to register for stream switch notifications: 0x%08X.", hr);
		return hr;
	}
	defer [&] { m_dev_enumerator->UnregisterEndpointNotificationCallback(this); };

	// Working loop.

	DebugLog("Enter main loop.");

	for (bool working = true; working; )
	{
		uint32_t seconds_to_sleeping = (m_cfg_no_sleep ? -1 : GetSecondsToSleeping());

		if (seconds_to_sleeping == 0)
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

		DWORD timeout = (m_is_retry_required || seconds_to_sleeping <= 30) ? 500 : (m_cfg_no_sleep ? INFINITE : 5000);

		switch (WaitForAny({ m_do_retry, m_do_restart, m_do_shutdown, global_stop_event }, timeout))
		{
		case WAIT_TIMEOUT:

			break;

		case WAIT_OBJECT_0 + 0:

			// Prevent multiple retries.
			while (WaitForOne(m_do_retry, 500) != WAIT_TIMEOUT);
			m_is_retry_required = true;
			break;

		case WAIT_OBJECT_0 + 1:

			// Prevent multiple restarts.
			while (WaitForOne(m_do_restart, 500) != WAIT_TIMEOUT);
			DebugLog("Restarting...");
			this->Restart();
			break;

		case WAIT_OBJECT_0 + 2:
		case WAIT_OBJECT_0 + 3:
		default:

			// We're done, exit the loop.
			DebugLog("Shutdown.");
			working = false;
			break;
		}
	}

	DebugLog("Leave main loop.");
	Stop();
	return hr;
}

FORCEINLINE HRESULT CSoundKeeper::Main()
{
	DebugThreadName("Main");

	DebugLog("Enter main thread.");

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
		DebugLog("Leave main thread. Exit code: 0.");
	}
	else
	{
		DebugLog("Leave main thread. Exit code: 0x%08X.", hr);
	}
#endif

	return hr;
}

#ifdef _CONSOLE

int main()
{
	if (const VS_FIXEDFILEINFO* ffi = GetFixedVersion())
	{
		DebugLog("Sound Keeper v%hu.%hu.%hu.%hu [%04hu/%02hu/%02hu] (" APP_ARCH ")",
			HIWORD(ffi->dwProductVersionMS),
			LOWORD(ffi->dwProductVersionMS),
			HIWORD(ffi->dwProductVersionLS),
			LOWORD(ffi->dwProductVersionLS),
			HIWORD(ffi->dwFileVersionMS),
			LOWORD(ffi->dwFileVersionMS),
			HIWORD(ffi->dwFileVersionLS),
			LOWORD(ffi->dwFileVersionLS)
		);
	}

	return CSoundKeeper::Main();
}

#else

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
	return CSoundKeeper::Main();
}

#endif
