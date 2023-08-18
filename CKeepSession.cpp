#include "Common.hpp"
#define INITGUID
#include "CKeepSession.hpp"
#define _USE_MATH_DEFINES
#include <math.h>
#ifdef ENABLE_MMCSS
#include <avrt.h>
#endif

bool CKeepSession::g_is_leaky_wasapi = false;

CKeepSession::CKeepSession(CSoundKeeper* soundkeeper, IMMDevice* endpoint)
	: m_soundkeeper(soundkeeper), m_endpoint(endpoint)
{
	m_endpoint->AddRef();
	m_soundkeeper->AddRef();

	if (HRESULT hr = m_endpoint->GetId(&m_device_id); FAILED(hr))
	{
		DebugLogError("Unable to get device ID: 0x%08X.", hr);
		m_curr_mode = RenderingMode::Invalid;
	}
}

CKeepSession::~CKeepSession(void)
{
	this->Stop();
	if (m_device_id) { CoTaskMemFree(m_device_id); }
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
		return false;
	}

	// Wait until rendering is started.
	if (WaitForAny({ m_is_started, m_render_thread }, INFINITE) != WAIT_OBJECT_0)
	{
		DebugLogError("Unable to start rendering.");
		this->Stop();
		return false;
	}

	return true;
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
		m_render_thread = NULL;
		this->ResetCurrent();
		m_interrupt = false;
	}
}

//
// Rendering thread.
//

DWORD APIENTRY CKeepSession::StartRenderingThread(LPVOID context)
{
	CKeepSession* renderer = static_cast<CKeepSession*>(context);

	DebugLog("Enter rendering thread. Device ID: '%S'.", renderer->GetDeviceId());

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

	DWORD result = renderer->RenderingThread();

#ifdef ENABLE_MMCSS
	if (mmcss_handle != NULL) AvRevertMmThreadCharacteristics(mmcss_handle);
#endif

	CoUninitialize();

	DebugLog("Leave rendering thread. Return code: %d.", result);
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
		DebugLog("Rendering thread mode: %d. Delay: %d.", m_curr_mode, delay);

		switch (WaitForOne(m_interrupt, delay))
		{
		case WAIT_OBJECT_0:

			DebugLog("Set new rendering thread mode: %d.", m_next_mode);
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

			DebugLog("Render. Device State: %d.", this->GetDeviceState());
			m_curr_mode = this->Rendering();
			if (g_is_leaky_wasapi && m_curr_mode == RenderingMode::WaitExclusive)
			{
				delay = 30;
			}
			break;

		case RenderingMode::WaitExclusive:

			if (g_is_leaky_wasapi)
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

			if (g_is_leaky_wasapi && m_play_attempts > 10)
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
		return exit_mode;
	}
	defer [&] { m_audio_client->Release(); };

	{
		// Get output format. Don't rely on it much since WASAPI reporting is not always accurate:
		// 24-bit compressed formats are reported as 16-bit; PCM int24 is reported as PCM int32.
		DebugLog("Getting output format...");
		IPropertyStore* properties = nullptr;
		hr = m_endpoint->OpenPropertyStore(STGM_READ, &properties);
		if (SUCCEEDED(hr))
		{
			PROPVARIANT out_format_prop;
			PropVariantInit(&out_format_prop);
			hr = properties->GetValue(PKEY_AudioEngine_DeviceFormat, &out_format_prop);
			if (SUCCEEDED(hr) && out_format_prop.vt == VT_BLOB)
			{
				auto out_format = reinterpret_cast<WAVEFORMATEX*>(out_format_prop.blob.pBlobData);
				m_out_sample_type = ParseSampleType(out_format);
			}
			else
			{
				DebugLogWarning("Unable to get output format of the device: 0x%08X.", hr);
			}
			PropVariantClear(&out_format_prop);
			SafeRelease(properties);
		}
		else
		{
			DebugLogWarning("Unable to get property store of the device: 0x%08X.", hr);
		}
	}

	{
		// Get mixer format. This is always float32.
		DebugLog("Getting mixing format...");
		WAVEFORMATEX* mix_format = nullptr;
		hr = m_audio_client->GetMixFormat(&mix_format);
		if (FAILED(hr))
		{
			DebugLogError("Unable to get mixing format on audio client: 0x%08X.", hr);
			return exit_mode;
		}
		defer [&] { CoTaskMemFree(mix_format); };

		m_mix_sample_type = ParseSampleType(mix_format);
		if (m_mix_sample_type != SampleType::Float32)
		{
			DebugLogError("Mixing format is not 32-bit float that is not supported.");
			return exit_mode;
		}

		m_channels_count = mix_format->nChannels;
		m_frame_size = mix_format->nBlockAlign;

		// Noise generation works best with the 48000Hz sample rate.
		if (m_stream_type == KeepStreamType::WhiteNoise || m_stream_type == KeepStreamType::BrownNoise || m_stream_type == KeepStreamType::PinkNoise)
		{
			DebugLog("Using 48000Hz sample rate for noise generation.");
			mix_format->nSamplesPerSec = 48000;
			mix_format->nAvgBytesPerSec= mix_format->nSamplesPerSec * mix_format->nBlockAlign;
		}

		m_sample_rate = mix_format->nSamplesPerSec;

		// Initialize WASAPI in timer driven mode.
		hr = m_audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM /*| AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY*/,
			static_cast<UINT64>(m_buffer_size_in_ms) * 10000, 0, mix_format, NULL);
	}

	if (FAILED(hr))
	{
		if (hr == AUDCLNT_E_DEVICE_IN_USE)
		{
			exit_mode = RenderingMode::WaitExclusive;
		}
		DebugLogError("Unable to initialize audio client: 0x%08X.", hr);
		return exit_mode;
	}

	// Retry on any errors below.
	exit_mode = RenderingMode::Retry;

	//
	// Retrieve the buffer size for the audio client.
	hr = m_audio_client->GetBufferSize(&m_buffer_size_in_frames);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get audio client buffer: 0x%08X.", hr);
		return exit_mode;
	}

	hr = m_audio_client->GetService(IID_PPV_ARGS(&m_render_client));
	if (FAILED(hr))
	{
		DebugLogError("Unable to get new render client: 0x%08X.", hr);
		return exit_mode;
	}
	defer [&] { m_render_client->Release(); };

	//
	// Register for session and endpoint change notifications.  
	hr = m_audio_client->GetService(IID_PPV_ARGS(&m_audio_session_control));
	if (FAILED(hr))
	{
		DebugLogError("Unable to retrieve session control: 0x%08X.", hr);
		return exit_mode;
	}
	defer [&] { m_audio_session_control->Release(); };
	hr = m_audio_session_control->RegisterAudioSessionNotification(this);
	if (FAILED(hr))
	{
		DebugLogError("Unable to register for stream switch notifications: 0x%08X.", hr);
		return exit_mode;
	}
	defer [&] { m_audio_session_control->UnregisterAudioSessionNotification(this); };

	// -------------------------------------------------------------------------
	// Rendering Loop
	// -------------------------------------------------------------------------

	DebugLog("Starting rendering...");

	// We need to pre-roll one buffer of data into the pipeline before starting.
	hr = this->Render();
	if (FAILED(hr))
	{
		DebugLogError("Can't render initial buffer: 0x%08X.", hr);
		return exit_mode;
	}

	hr = m_audio_client->Start();
	if (FAILED(hr))
	{
		DebugLogError("Unable to start render client: 0x%08X.", hr);
		return exit_mode;
	}

	DebugLog("Enter rendering loop.");

	m_play_attempts = 0;

	DWORD timeout = m_stream_type == KeepStreamType::None ? INFINITE : (m_buffer_size_in_ms / 2 + m_buffer_size_in_ms / 4);
	for (bool working = true; working; ) switch (WaitForOne(m_interrupt, timeout))
	{
	case WAIT_TIMEOUT: // Timeout.

		// Provide the next buffer of samples.
		hr = this->Render();
		if (FAILED(hr))
		{
			working = false;
		}
		break;

	case WAIT_OBJECT_0 + 0: // m_interrupt.

		// We're done, exit the loop.
		exit_mode = m_next_mode;
		working = false;
		break;

	default:

		// Should never happen.
		exit_mode = RenderingMode::Invalid;
		working = false;
		break;
	}

	DebugLog("Leave rendering loop. Stopping audio client...");

	m_audio_client->Stop();
	return exit_mode;
}

CKeepSession::SampleType CKeepSession::ParseSampleType(WAVEFORMATEX* format)
{
	SampleType result = SampleType::Unknown;

	if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
		|| (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
	{
		DebugLog("Format: PCM %dch %dHz %d-bit float.", format->nChannels, format->nSamplesPerSec, format->wBitsPerSample);
		if (format->wBitsPerSample == 32)
		{
			result = SampleType::Float32;
		}
		else
		{
			DebugLogWarning("Unsupported PCM float sample type.");
		}
	}
	else if (format->wFormatTag == WAVE_FORMAT_PCM
		|| format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
	{
		DebugLog("Format: PCM %dch %dHz %d-bit integer.", format->nChannels, format->nSamplesPerSec, format->wBitsPerSample);
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
			DebugLogWarning("Unsupported PCM integer sample type.");
		}
	}
	else
	{
#ifdef _CONSOLE
		if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
		{
			DebugLogWarning("Unrecognized format: 0x%04hX %dch %dHz %d-bit.", format->wFormatTag,
				format->nChannels, format->nSamplesPerSec, format->wBitsPerSample);
		}
		else
		{
			GUID& guid = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat;
			DebugLogWarning("Unrecognized format: {%08lx-%04hx-%04hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx} %dch %dHz %d-bit.",
				guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
				guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7],
				format->nChannels, format->nSamplesPerSec, format->wBitsPerSample);
		}
#endif
	}

	return result;
}

HRESULT CKeepSession::Render()
{
	HRESULT hr = S_OK;

	TraceLog("Render.");

	if (m_stream_type == KeepStreamType::None)
	{
		return hr;
	}

	// Find out how much of the buffer *isn't* available (is padding).
	UINT32 padding;
	hr = m_audio_client->GetCurrentPadding(&padding);
	if (FAILED(hr))
	{
		DebugLogError("Failed to get padding: 0x%08X.", hr);
		return hr;
	}

	// Calculate the number of frames available. It can be 0 right after waking PC up after sleeping.
	UINT32 need_frames = m_buffer_size_in_frames - padding;
	if (need_frames == 0)
	{
		DebugLogWarning("None samples were consumed. Was PC sleeping?");
		return S_OK;
	}

	BYTE* p_data;
	hr = m_render_client->GetBuffer(need_frames, &p_data);
	if (FAILED(hr))
	{
		DebugLogError("Failed to get buffer: 0x%08X.", hr);
		return hr;
	}

	// Generate sound.

	DWORD render_flags = NULL;

	uint64_t play_frames = static_cast<uint64_t>(m_play_seconds * m_sample_rate);
	uint64_t wait_frames = static_cast<uint64_t>(m_wait_seconds * m_sample_rate);
	uint64_t fade_frames = static_cast<uint64_t>(m_fade_seconds * m_sample_rate);

	if (!wait_frames && !fade_frames)
	{
		play_frames = 0;
	}
	else if (!play_frames)
	{
		wait_frames = 0;
	}

	if (play_frames)
	{
		fade_frames = std::min(fade_frames, play_frames / 2);
	}

	uint64_t period_frames = play_frames + wait_frames;

	if (period_frames && play_frames <= m_curr_frame && (m_curr_frame + need_frames) <= period_frames)
	{
		// Just silence whole time.
		render_flags = AUDCLNT_BUFFERFLAGS_SILENT;
		m_curr_frame = (m_curr_frame + need_frames) % period_frames;
	}
	else if (m_stream_type == KeepStreamType::Fluctuate && m_frequency)
	{
		uint64_t once_in_frames = std::max(uint64_t(double(m_sample_rate) / m_frequency), 2ULL);

		for (size_t i = 0; i < need_frames; i++)
		{
			uint32_t sample = 0;

			if ((!period_frames || m_curr_frame < play_frames) && m_curr_frame % once_in_frames == 0)
			{
				// 0x38000100 = 3.051851E-5 = 1.0/32767. Minimal 16-bit deviation from 0.
				// 0x34000001 = 1.192093E-7 = 1.0/8388607. Minimal 24-bit deviation from 0.
				sample = (m_out_sample_type == SampleType::Int16) ? 0x38000100 : 0x34000001;

				// Negate each odd time.
				if ((m_curr_frame / once_in_frames) & 1)
				{
					sample |= 0x80000000;
				}
			}

			for (size_t j = 0; j < m_channels_count; j++)
			{
				*reinterpret_cast<uint32_t*>(p_data + j * sizeof(float)) = sample;
			}

			p_data += m_frame_size;
			m_curr_frame++;
			if (period_frames) { m_curr_frame %= period_frames; }
		}
	}
	else if (m_stream_type == KeepStreamType::Sine && m_frequency && m_amplitude)
	{
		double theta_increment = (std::min(m_frequency, m_sample_rate / 2.0) * (M_PI*2)) / double(m_sample_rate);

		for (size_t i = 0; i < need_frames; i++)
		{
			double amplitude = m_amplitude;

			if (period_frames || m_curr_frame < fade_frames)
			{
				if (m_curr_frame < fade_frames)
				{
					// Fade in.
					double fade_volume = (1.0 / fade_frames) * m_curr_frame;
					amplitude *= pow(fade_volume, 2);
				}
				else if (!play_frames || m_curr_frame < (play_frames - fade_frames))
				{
					// Max volume.
				}
				else if (m_curr_frame < play_frames)
				{
					// Fade out.
					double fade_volume = (1.0 / fade_frames) * (play_frames - m_curr_frame);
					amplitude *= pow(fade_volume, 2);
				}
				else
				{
					// Silence.
					amplitude = 0;
				}
			}

			float sample = 0;

			if (amplitude)
			{
				sample = float(sin(m_curr_theta) * amplitude);
				m_curr_theta += theta_increment;
			}

			for (size_t j = 0; j < m_channels_count; j++)
			{
				*reinterpret_cast<float*>(p_data + j * sizeof(float)) = sample;
			}

			p_data += m_frame_size;
			m_curr_frame++;
			if (period_frames) { m_curr_frame %= period_frames; }
		}
	}
	else if ((m_stream_type == KeepStreamType::WhiteNoise || m_stream_type == KeepStreamType::BrownNoise || m_stream_type == KeepStreamType::PinkNoise) && m_amplitude)
	{
		uint64_t lcg_state = __rdtsc();

		for (size_t i = 0; i < need_frames; i++)
		{
			double amplitude = m_amplitude;

			if (period_frames || m_curr_frame < fade_frames)
			{
				if (m_curr_frame < fade_frames)
				{
					// Fade in.
					double fade_volume = (1.0 / fade_frames) * m_curr_frame;
					amplitude *= pow(fade_volume, 2);
				}
				else if (!play_frames || m_curr_frame < (play_frames - fade_frames))
				{
					// Max volume.
				}
				else if (m_curr_frame < play_frames)
				{
					// Fade out.
					double fade_volume = (1.0 / fade_frames) * (play_frames - m_curr_frame);
					amplitude *= pow(fade_volume, 2);
				}
				else
				{
					// Silence.
					amplitude = 0;
				}
			}

			float sample = 0;

			if (amplitude)
			{
				lcg_state = lcg_state * 6364136223846793005ULL + 1; // LCG from Musl.
				double value = (double((lcg_state >> 32) & 0x7FFFFFFF) / double(0x7FFFFFFFU)) * 2.0 - 1.0; // -1 .. 1

				if (m_stream_type == KeepStreamType::BrownNoise)
				{
					// Brown Noise from SoX + a leaky integrator to reduce low frequency humming.
					m_curr_value += value * (1.0 / 16);
					m_curr_value /= 1.02; // The leaky integrator.
					m_curr_value = fmod(m_curr_value, 4);
					value = m_curr_value;

					// Normalize values out of the -1..1 range using "mirroring".
					// Example: 0.8, 0.9, 1.0, 0.9, 0.8, ..., -0.8, -0.9, -1.0, -0.9, -0.8, ...
					// Precondition: value must be between -4.0 and 4.0.
					if (value < -1.0 || 1.0 < value)
					{
						double sign = (value < 0.0) ? -1.0 : 1.0;
						value = fabs(value);
						value = ((value <= 3.0) ? (2.0 - value) : (value - 4.0)) * sign;
					}
				}
				else if (m_stream_type == KeepStreamType::PinkNoise)
				{
					// Paul Kellet's method.
					double white = value;
					m_curr_state[0] = 0.99886 * m_curr_state[0] + white * 0.0555179;
					m_curr_state[1] = 0.99332 * m_curr_state[1] + white * 0.0750759;
					m_curr_state[2] = 0.96900 * m_curr_state[2] + white * 0.1538520;
					m_curr_state[3] = 0.86650 * m_curr_state[3] + white * 0.3104856;
					m_curr_state[4] = 0.55000 * m_curr_state[4] + white * 0.5329522;
					m_curr_state[5] = -0.7616 * m_curr_state[5] - white * 0.0168980;
					value = m_curr_state[0] + m_curr_state[1] + m_curr_state[2] + m_curr_state[3] + m_curr_state[4] + m_curr_state[5] + m_curr_state[6] + white * 0.5362;
					value *= 0.11; // (roughly) compensate for gain.
					m_curr_state[6] = white * 0.115926;
				}

				sample = float(value * amplitude);
			}

			for (size_t j = 0; j < m_channels_count; j++)
			{
				*reinterpret_cast<float*>(p_data + j * sizeof(float)) = sample;
			}

			p_data += m_frame_size;
			m_curr_frame++;
			if (period_frames) { m_curr_frame %= period_frames; }
		}
	}
	else
	{
		// ZeroMemory(p_data, static_cast<SIZE_T>(m_frame_size) * need_frames);
		render_flags = AUDCLNT_BUFFERFLAGS_SILENT;
	}

	hr = m_render_client->ReleaseBuffer(need_frames, render_flags);
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
		return exit_mode;
	}
	defer [&] { as_manager->Release(); };

	IAudioSessionEnumerator* session_list = nullptr;
	hr = as_manager->GetSessionEnumerator(&session_list);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get session enumerator: 0x%08X.", hr);
		return exit_mode;
	}
	defer [&] { session_list->Release(); };

	int session_count = 0;
	hr = session_list->GetCount(&session_count);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get session count: 0x%08X.", hr);
		return exit_mode;
	}

	// Retry on any errors below.
	exit_mode = RenderingMode::Retry;

	IAudioSessionControl* session_control = nullptr;
	defer [&] { SafeRelease(session_control); };

	// Find active session on this device.
	for (int index = 0 ; index < session_count ; index++)
	{
		hr = session_list->GetSession(index, &session_control);
		if (FAILED(hr))
		{
			DebugLogError("Unable to get session #%d: 0x%08X.", index, hr);
			return exit_mode;
		}

		AudioSessionState state = AudioSessionStateInactive;
		hr = session_control->GetState(&state);
		if (FAILED(hr))
		{
			DebugLogError("Unable to get session #%d state: 0x%08X.", index, hr);
			return exit_mode;
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
			return exit_mode;
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
		// m_soundkeeper->FireShutdown();
	}
	return S_OK;
}
