#include "Common.hpp"
#define INITGUID
#include "CKeepSession.hpp"
#define _USE_MATH_DEFINES
#include <math.h>
#ifdef ENABLE_MMCSS
#include <avrt.h>
#endif

extern "C" NTSYSAPI NTSTATUS WINAPI RtlGetVersion(PRTL_OSVERSIONINFOW lpVersionInformation);

ULONG GetWinBuildNumber()
{
	static ULONG build_number = 0;

	if (build_number != 0)
	{
		return build_number;
	}

	RTL_OSVERSIONINFOW os_info = {0};
	os_info.dwOSVersionInfoSize = sizeof(os_info);
	RtlGetVersion(&os_info);
	build_number = os_info.dwBuildNumber;
	return build_number;
}

bool IsBuggyWasapi()
{
	ULONG build_number = GetWinBuildNumber();
	// Windows 7 is not buggy. Windows 8-10 leak handles and shared memory. Windows 11 has this bug fixed.
	bool is_buggy = 7601 < build_number && build_number < 22000;
	DebugLog("Windows Build Number: %u%s.", build_number, is_buggy ? " (buggy)" : "");
	return is_buggy;
}

static bool g_is_buggy_wasapi = IsBuggyWasapi();

CKeepSession::CKeepSession(CSoundKeeper* soundkeeper, IMMDevice* endpoint)
	: m_soundkeeper(soundkeeper), m_endpoint(endpoint)
{
	m_endpoint->AddRef();
	m_soundkeeper->AddRef();
}

CKeepSession::~CKeepSession(void)
{
	this->Stop();
	SafeRelease(m_endpoint);
	SafeRelease(m_soundkeeper);
}

HRESULT STDMETHODCALLTYPE CKeepSession::QueryInterface(REFIID iid, void **object)
{
	if (object == NULL)
	{
		return E_POINTER;
	}
	*object = NULL;

	if (iid == IID_IUnknown)
	{
		*object = static_cast<IUnknown *>(static_cast<IAudioSessionEvents *>(this));
		AddRef();
	}
	else if (iid == __uuidof(IAudioSessionEvents))
	{
		*object = static_cast<IAudioSessionEvents *>(this);
		AddRef();
	}
	else
	{
		return E_NOINTERFACE;
	}
	return S_OK;
}

ULONG STDMETHODCALLTYPE CKeepSession::AddRef()
{
	return InterlockedIncrement(&m_ref_count);
}

ULONG STDMETHODCALLTYPE CKeepSession::Release()
{
	ULONG result = InterlockedDecrement(&m_ref_count);
	if (result == 0)
	{
		delete this;
	}
	return result;
}

//
// Initialize and start the renderer.
bool CKeepSession::Start()
{
	ScopedLock lock(m_mutex);

	if (!this->IsValid()) { return false; }
	if (this->IsStarted()) { return true; }

	this->Stop();

	//
	// Now create the thread which is going to drive the renderer.
	m_render_thread = CreateThread(NULL, 0, StartRenderingThread, this, 0, NULL);
	if (m_render_thread == NULL)
	{
		DebugLogError("Unable to create rendering thread: 0x%08X.", GetLastError());
		goto error;
	}

	// Wait until rendering is started.
	if (WaitForAny({ m_is_started, m_render_thread }, INFINITE) != WAIT_OBJECT_0)
	{
		DebugLogError("Unable to start rendering.");
		goto error;
	}

	return true;

error:

	this->Stop();
	return false;
}

//
// Stop the renderer and free all the resources.
void CKeepSession::Stop()
{
	ScopedLock lock(m_mutex);

	if (m_render_thread)
	{
		this->DeferNextMode(RenderingMode::Stop);
		WaitForOne(m_render_thread, INFINITE);
		CloseHandle(m_render_thread);
		m_theta = 0;
		m_render_thread = NULL;
		m_interrupt = false;
	}
}

//
// Rendering thread.
//

DWORD APIENTRY CKeepSession::StartRenderingThread(LPVOID context)
{
	DebugLog("Rendering thread started.");

	if (HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE); FAILED(hr))
	{
		DebugLogError("Unable to initialize COM in rendering thread: 0x%08X.", hr);
		return 1;
	}

#ifdef ENABLE_MMCSS
	HANDLE mmcss_handle = NULL;
	DWORD mmcss_task_index = 0;
	mmcss_handle = AvSetMmThreadCharacteristics(L"Audio", &mmcss_task_index);
	if (mmcss_handle == NULL)
	{
		DebugLogError("Unable to enable MMCSS on rendering thread: 0x%08X.", GetLastError());
	}
#endif

	CKeepSession* renderer = static_cast<CKeepSession*>(context);
	DWORD result = renderer->RenderingThread();

#ifdef ENABLE_MMCSS
	if (mmcss_handle != NULL) AvRevertMmThreadCharacteristics(mmcss_handle);
#endif

	CoUninitialize();

	DebugLog("Rendering thread finished. Return code: %d.", result);
	return result;
}

DWORD CKeepSession::RenderingThread()
{
	m_is_started = true;
	m_curr_mode = RenderingMode::Render;
	m_play_attempts = 0;
	m_wait_attempts = 0;

	DWORD delay = 0;
	bool loop = true;

	while (loop)
	{
		switch (WaitForOne(m_interrupt, delay))
		{
		case WAIT_OBJECT_0:

			m_curr_mode = m_next_mode;
			break;

		case WAIT_TIMEOUT:

			break;

		default:

			// Shouldn't happen.
			m_curr_mode = RenderingMode::Invalid;
			break;
		}
		delay = 0;

		switch (m_curr_mode)
		{
		case RenderingMode::Render:

			DebugLog("Render.");
			m_curr_mode = this->Rendering();
			if (g_is_buggy_wasapi && m_curr_mode == RenderingMode::WaitExclusive)
			{
				delay = 30;
			}
			break;

		case RenderingMode::WaitExclusive:

			if (g_is_buggy_wasapi)
			{
				DebugLog("Wait until exclusive session is finised.");
				m_curr_mode = this->WaitExclusive();
				if (m_curr_mode == RenderingMode::WaitExclusive)
				{
					delay = 100;
				}
				break;
			}
			[[fallthrough]];

		case RenderingMode::Retry:

			if (g_is_buggy_wasapi && m_play_attempts > 10)
			{
				DebugLog("Attempts limit. Stop.");
				m_curr_mode = RenderingMode::Stop;
				break;
			}

			// m_play_attempts is 0 when it was interrupted while playing (when rendering was initialized without errors).
			delay = (m_play_attempts == 0 ? 100UL : 750UL);

			DebugLog("Retry in %dms. Attempt: #%d.", delay, m_play_attempts);
			m_curr_mode = RenderingMode::Render;
			break;

		// case RenderingMode::Stop:
		// case RenderingMode::Invalid:
		default:

			loop = false;
			break;
		}
	}

	m_is_started = false;
	return m_curr_mode == RenderingMode::Invalid;
}

CKeepSession::RenderingMode CKeepSession::Rendering()
{
	RenderingMode exit_mode;
	HRESULT hr;

	m_play_attempts++;

	// -------------------------------------------------------------------------
	// Rendering Init
	// -------------------------------------------------------------------------

	// Any errors below should invalidate this session.
	exit_mode = RenderingMode::Invalid;

	//
	// Now activate an IAudioClient object on our preferred endpoint.
	hr = m_endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void **>(&m_audio_client));
	if (FAILED(hr))
	{
		DebugLogError("Unable to activate audio client: 0x%08X.", hr);
		goto free;
	}

	//
	// Get the output format. This may be int16 or int24 depending on the shared mode used.
	{
		IPropertyStore* properties = nullptr;
		hr = m_endpoint->OpenPropertyStore(STGM_READ, &properties);
		if (SUCCEEDED(hr))
		{
			PROPVARIANT out_format;
			PropVariantInit(&out_format);
			hr = properties->GetValue(PKEY_AudioEngine_DeviceFormat, &out_format);
			if (SUCCEEDED(hr) && out_format.vt == VT_BLOB)
			{
				m_out_sample_type = GetSampleType((WAVEFORMATEX*)out_format.blob.pBlobData);
			}
			PropVariantClear(&out_format);
			SafeRelease(properties);
		}
	}

	//
	// Get the mixer format. This is usually float32.
	WAVEFORMATEX* mix_format = nullptr;
	hr = m_audio_client->GetMixFormat(&mix_format);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get mix format on audio client: 0x%08X.", hr);
		goto free;
	}

	m_mix_sample_type = GetSampleType(mix_format);
	m_frame_size = mix_format->nBlockAlign;
	m_sample_rate = mix_format->nSamplesPerSec;
	m_channels_count = mix_format->nChannels;

	//
	// Initialize WASAPI in timer driven mode.
	hr = m_audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, static_cast<UINT64>(m_buffer_size_in_ms) * 10000, 0, mix_format, NULL);
	CoTaskMemFree(mix_format); mix_format = nullptr;
	if (FAILED(hr))
	{
		if (hr == AUDCLNT_E_DEVICE_IN_USE)
		{
			exit_mode = RenderingMode::WaitExclusive;
		}
		DebugLogError("Unable to initialize audio client: 0x%08X.", hr);
		goto free;
	}

	// Retry on any errors below.
	exit_mode = RenderingMode::Retry;

	//
	// Retrieve the buffer size for the audio client.
	hr = m_audio_client->GetBufferSize(&m_buffer_size_in_frames);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get audio client buffer: 0x%08X.", hr);
		goto free;
	}

	hr = m_audio_client->GetService(IID_PPV_ARGS(&m_render_client));
	if (FAILED(hr))
	{
		DebugLogError("Unable to get new render client: 0x%08X.", hr);
		goto free;
	}

	//
	// Register for session and endpoint change notifications.  
	hr = m_audio_client->GetService(IID_PPV_ARGS(&m_audio_session_control));
	if (FAILED(hr))
	{
		DebugLogError("Unable to retrieve session control: 0x%08X.", hr);
		goto free;
	}
	hr = m_audio_session_control->RegisterAudioSessionNotification(this);
	if (FAILED(hr))
	{
		DebugLogError("Unable to register for stream switch notifications: 0x%08X.", hr);
		goto free;
	}

	// -------------------------------------------------------------------------
	// Rendering Loop
	// -------------------------------------------------------------------------

	// We want to pre-roll one buffer of data into the pipeline. That way the audio engine won't glitch on startup.  
	hr = this->Render();
	if (FAILED(hr))
	{
		DebugLogError("Can't render initial buffer: 0x%08X.", hr);
		goto free;
	}

	hr = m_audio_client->Start();
	if (FAILED(hr))
	{
		DebugLogError("Unable to start render client: 0x%08X.", hr);
		goto free;
	}

	m_play_attempts = 0;

	while (true) switch (WaitForOne(m_interrupt, m_buffer_size_in_ms / 2 + m_buffer_size_in_ms / 4))
	{
	case WAIT_TIMEOUT: // Timeout.

		// Provide the next buffer of samples.
		hr = this->Render();
		if (FAILED(hr))
		{
			goto stop;
		}
		break;

	case WAIT_OBJECT_0 + 0: // m_interrupt.

		// We're done, exit the loop.
		exit_mode = m_next_mode;
		goto stop;

	default:

		// Should never happen.
		exit_mode = RenderingMode::Invalid;
		goto stop;
	}

stop:

	m_audio_client->Stop();

	// -------------------------------------------------------------------------
	// Rendering Cleanup
	// -------------------------------------------------------------------------

free:

	if (m_audio_session_control)
	{
		m_audio_session_control->UnregisterAudioSessionNotification(this);
		SafeRelease(m_audio_session_control);
	}

	SafeRelease(m_render_client);
	SafeRelease(m_audio_client);

	return exit_mode;
}

CKeepSession::SampleType CKeepSession::GetSampleType(WAVEFORMATEX* format)
{
	SampleType result = SampleType::Unknown;
	if (format->wFormatTag == WAVE_FORMAT_PCM
		|| format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
	{
		DebugLog("Format: PCM %d-bit integer.", format->wBitsPerSample);
		if (format->wBitsPerSample == 16)
		{
			result = SampleType::Int16;
		}
		else if (format->wBitsPerSample == 24)
		{
			result = SampleType::Int24;
		}
		else if (format->wBitsPerSample == 32)
		{
			result = SampleType::Int32;
		}
		else
		{
			DebugLogError("Unsupported PCM integer sample type.");
		}
	}
	else if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
		|| (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
	{
		DebugLog("Format: PCM %d-bit float.", format->wBitsPerSample);
		if (format->wBitsPerSample == 32)
		{
			result = SampleType::Float32;
		}
		else
		{
			DebugLogError("Unsupported PCM float sample type.");
		}
	}
	else
	{
		DebugLogError("Unrecognized sample format.");
	}
	return result;
}

HRESULT CKeepSession::Render()
{
	HRESULT hr = S_OK;
	BYTE* p_data;
	UINT32 padding;
	UINT32 frames_available;

	// We want to find out how much of the buffer *isn't* available (is padding).
	hr = m_audio_client->GetCurrentPadding(&padding);
	if (FAILED(hr))
	{
		DebugLogError("Failed to get padding: 0x%08X.", hr);
		return hr;
	}

	// Calculate the number of frames available.
	frames_available = m_buffer_size_in_frames - padding;
	if (m_stream_type == KeepStreamType::Fluctuate)
	{
		frames_available &= 0xFFFFFFFC; // Must be a multiple of 4.
	}
	// It can happen right after waking PC up after sleeping, so just do nothing.
	if (frames_available == 0) { return S_OK; }

	hr = m_render_client->GetBuffer(frames_available, &p_data);
	if (FAILED(hr))
	{
		DebugLogError("Failed to get buffer: 0x%08X.", hr);
		return hr;
	}

	DWORD render_flags = NULL;

	if (m_stream_type == KeepStreamType::Fluctuate && m_mix_sample_type == SampleType::Int16)
	{
		UINT32 n = 0;
		constexpr static INT16 tbl[] = { -1, 0, 1, 0 };
		for (size_t i = 0; i < frames_available; i++)
		{
			for (size_t j = 0; j < m_channels_count; j++)
			{
				*reinterpret_cast<INT16*>(p_data + j * 2) = tbl[n];
			}
			p_data += m_frame_size;
			n = ++n % 4;
		}
	}
	else if (m_stream_type == KeepStreamType::Fluctuate && m_mix_sample_type == SampleType::Float32)
	{
		// 0xb8000100 = -3.051851E-5 = -1.0/32767.
		// 0x38000100 =  3.051851E-5 =  1.0/32767.
		constexpr static UINT32 tbl16[] = { 0xb8000100, 0, 0x38000100, 0 };
		// 0xb4000001 = -1.192093E-7 = -1.0/8388607.
		// 0x34000001 =  1.192093E-7 =  1.0/8388607.
		constexpr static UINT32 tbl24[] = { 0xb4000001, 0, 0x34000001, 0 };
		// 0xb0000000 = -4.656612E-10 = -1.0/2147483647.
		// 0x30000000 =  4.656612E-10 =  1.0/2147483647.
		constexpr static UINT32 tbl32[] = { 0xb0000000, 0, 0x30000000, 0 };

		UINT32 n = 0;
		const UINT32* tbl = (m_out_sample_type == SampleType::Int24) ? tbl24
			: (m_out_sample_type == SampleType::Int32 || m_out_sample_type == SampleType::Float32) ? tbl32
			: tbl16;

		for (size_t i = 0; i < frames_available; i++)
		{
			for (size_t j = 0; j < m_channels_count; j++)
			{
				*reinterpret_cast<UINT32*>(p_data + j * 4) = tbl[n];
			}
			p_data += m_frame_size;
			n = ++n % 4;
		}
	}
	else if (m_stream_type == KeepStreamType::Sine && m_mix_sample_type == SampleType::Float32 && m_frequency != 0.0 && m_amplitude != 0.0)
	{
		double theta_increment = (m_frequency * (M_PI*2)) / (double)m_sample_rate;
		for (size_t i = 0; i < frames_available; i++)
		{
			float sample = (float)(sin(m_theta) * m_amplitude);
			for (size_t j = 0; j < m_channels_count; j++)
			{
				*reinterpret_cast<float*>(p_data + j * sizeof(float)) = sample;
			}
			p_data += m_frame_size;
			m_theta += theta_increment;
		}
	}
	else
	{
		// ZeroMemory(p_data, static_cast<SIZE_T>(m_frame_size) * frames_available);
		render_flags = AUDCLNT_BUFFERFLAGS_SILENT;
	}

	hr = m_render_client->ReleaseBuffer(frames_available, render_flags);
	if (FAILED(hr))
	{
		DebugLogError("Failed to release buffer: 0x%08X.", hr);
		return hr;
	}

	return S_OK;
}

CKeepSession::RenderingMode CKeepSession::WaitExclusive()
{
	RenderingMode exit_mode;
	HRESULT hr;

	// Any errors below should invalidate this session.
	exit_mode = RenderingMode::Invalid;

	IAudioSessionManager2* as_manager = nullptr;
	hr = m_endpoint->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void**>(&as_manager));
	if (FAILED(hr))
	{
		DebugLogError("Unable to activate audio session manager: 0x%08X.", hr);
		goto free;
	}

	IAudioSessionEnumerator* session_list = NULL;
	hr = as_manager->GetSessionEnumerator(&session_list);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get session enumerator: 0x%08X.", hr);
		goto free;
	}

	int session_count = 0;
	hr = session_list->GetCount(&session_count);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get session count: 0x%08X.", hr);
		goto free;
	}

	// Retry on any errors below.
	exit_mode = RenderingMode::Retry;

	IAudioSessionControl* session_control = nullptr;

	// Find active session on this device.
	for (int index = 0 ; index < session_count ; index++)
	{
		hr = session_list->GetSession(index, &session_control);
		if (FAILED(hr))
		{
			DebugLogError("Unable to get session #%d: 0x%08X.", index, hr);
			goto free;
		}

		AudioSessionState state = AudioSessionStateInactive;
		hr = session_control->GetState(&state);
		if (FAILED(hr))
		{
			DebugLogError("Unable to get session #%d state: 0x%08X.", index, hr);
			goto free;
		}

		if (state != AudioSessionStateActive)
		{
			SafeRelease(session_control);
		}
		else
		{
			break;
		}
	}

	if (!session_control)
	{
		if (++m_wait_attempts < 100)
		{
			DebugLog("No active sessions found, try again.");
			exit_mode = RenderingMode::WaitExclusive;
		}
		else
		{
			DebugLog("No active sessions found, tried too many times. Try to play.");
			m_wait_attempts = 0;
			exit_mode = RenderingMode::Retry;
		}
	}
	else
	{
		m_wait_attempts = 0;
		hr = session_control->RegisterAudioSessionNotification(this);
		if (FAILED(hr))
		{
			DebugLogError("Unable to register for stream switch notifications: 0x%08X.", hr);
			goto free;
		}

		// Wait until we receive a notification that the streem is inactive.
		switch (WaitForOne(m_interrupt))
		{
		case WAIT_OBJECT_0: // m_interrupt.

			// We're done, exit the loop.
			exit_mode = m_next_mode;
			break;

		default:

			// Should never happen.
			exit_mode = RenderingMode::Invalid;
			break;
		}

		session_control->UnregisterAudioSessionNotification(this);
	}

free:

	SafeRelease(session_control);
	SafeRelease(session_list);
	SafeRelease(as_manager);

	return exit_mode;
}

//
// Called when state of an audio session is changed.
HRESULT CKeepSession::OnStateChanged(AudioSessionState NewState)
{
	if (m_curr_mode != RenderingMode::WaitExclusive)
	{
		return S_OK;
	}

	// On stop, it becomes AudioSessionStateInactive (0), and then AudioSessionStateExpired (2).
	DebugLog("State of the exclusive mode session is changed: %d.", NewState);
	this->DeferNextMode(RenderingMode::Retry);
	return S_OK;
};

//
// Called when an audio session is disconnected.
HRESULT CKeepSession::OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason)
{
	if (m_curr_mode != RenderingMode::Render)
	{
		return S_OK;
	}

	switch (DisconnectReason)
	{
	case DisconnectReasonFormatChanged:

		DebugLog("Session is disconnected with reason %d. Retry.", DisconnectReason);
		this->DeferNextMode(RenderingMode::Retry);
		break;

	case DisconnectReasonExclusiveModeOverride:

		DebugLog("Session is disconnected with reason %d. WaitExclusive.", DisconnectReason);
		this->DeferNextMode(RenderingMode::WaitExclusive);
		break;

	default:

		DebugLog("Session is disconnected with reason %d. Restart.", DisconnectReason);
		this->DeferNextMode(RenderingMode::Invalid);
		m_soundkeeper->FireRestart();
		break;
	}

	return S_OK;
}

HRESULT CKeepSession::OnSimpleVolumeChanged(float NewSimpleVolume, BOOL NewMute, LPCGUID EventContext)
{
	if (NewMute)
	{
		// Shutdown Sound Keeper when muted.
		m_soundkeeper->FireShutdown();
	}
	return S_OK;
}
