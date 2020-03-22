#include "StdAfx.h"
#define INITGUID
#include "CKeepSession.hpp"

#ifdef ENABLE_MMCSS
#include <avrt.h>
#endif

CKeepSession::CKeepSession(CSoundKeeper* soundkeeper, IMMDevice* endpoint)
	: m_soundkeeper(soundkeeper), m_endpoint(endpoint)
{
	m_endpoint->AddRef();
	m_soundkeeper->AddRef();
}

CKeepSession::~CKeepSession(void)
{
	this->Stop();
	SafeRelease(&m_endpoint);
	SafeRelease(&m_soundkeeper);
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

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		DebugLogError("Unable to initialize COM in render thread: 0x%08X.", hr);
		return 1;
	}

#ifdef ENABLE_MMCSS
	HANDLE mmcss_handle = NULL;
	DWORD mmcss_task_index = 0;
	mmcss_handle = AvSetMmThreadCharacteristics(L"Audio", &mmcss_task_index);
	if (mmcss_handle == NULL)
	{
		DebugLogError("Unable to enable MMCSS on render thread: 0x%08X.", GetLastError());
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
	m_attempts = 0;

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
			break;

		case RenderingMode::WaitExclusive:

			// TODO: Wait until exclusive session is over.

		case RenderingMode::Retry:

			/*
			if (m_attempts > 3)
			{
				DebugLog("Attempts limit. Stop.");
				m_curr_mode = RenderingMode::Stop;
				break;
			}
			*/

			// m_attempts is 0 when it was interrupted while playing (when rendering was initialized without errors).
			delay = (m_attempts == 0 ? 100UL : 750UL);

			DebugLog("Retry in %dms. Attempts: #%d.", delay, m_attempts);
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

	m_attempts++;

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
	// Load the MixFormat. This may differ depending on the shared mode used.
	hr = m_audio_client->GetMixFormat(&m_mix_format);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get mix format on audio client: 0x%08X.", hr);
		goto free;
	}

#if defined(ENABLE_INAUDIBLE) || defined(_DEBUG)

	m_frame_size = m_mix_format->nBlockAlign;
	m_channels_count = m_mix_format->nChannels;

	// Determine what kind of samples are being rendered.
	m_sample_type = k_sample_type_unknown;
	if (m_mix_format->wFormatTag == WAVE_FORMAT_PCM
		|| m_mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE *>(m_mix_format)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
	{
		DebugLog("Format: PCM %d-bit integer.", m_mix_format->wBitsPerSample);
		if (m_mix_format->wBitsPerSample == 16)
		{
			m_sample_type = k_sample_type_int16;
		}
		else
		{
			DebugLogError("Unsupported PCM integer sample type.");
		}
	}
	else if (m_mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
		|| (m_mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE *>(m_mix_format)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
	{
		DebugLog("Format: PCM %d-bit float.", m_mix_format->wBitsPerSample);
		if (m_mix_format->wBitsPerSample == 32)
		{
			m_sample_type = k_sample_type_float32;
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

#endif

	//
	// Initialize WASAPI in timer driven mode.
	hr = m_audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, static_cast<UINT64>(m_buffer_size_in_ms) * 10000, 0, m_mix_format, NULL);
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

	m_attempts = 0;

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
		SafeRelease(&m_audio_session_control);
	}

	if (m_mix_format)
	{
		CoTaskMemFree(m_mix_format);
		m_mix_format = NULL;
	}

	SafeRelease(&m_render_client);
	SafeRelease(&m_audio_client);

	return exit_mode;
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
#if defined(ENABLE_INAUDIBLE)
	frames_available &= 0xFFFFFFFC; // Must be a multiple of 4.
#endif
	// It can happen right after waking PC up after sleeping, so just do nothing.
	if (frames_available == 0) { return S_OK; }

	hr = m_render_client->GetBuffer(frames_available, &p_data);
	if (FAILED(hr))
	{
		DebugLogError("Failed to get buffer: 0x%08X.", hr);
		return hr;
	}

#if defined(ENABLE_INAUDIBLE)

	DWORD render_flags = NULL;

	if (m_sample_type == k_sample_type_int16)
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
	else if (m_sample_type == k_sample_type_float32)
	{
		UINT32 n = 0;
		// 0xb8000100 = -3.051851E-5 = -1.0/32767.
		// 0x38000100 =  3.051851E-5 =  1.0/32767.
		constexpr static UINT32 tbl[] = { 0xb8000100, 0, 0x38000100, 0 };
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
	else
	{
		render_flags = AUDCLNT_BUFFERFLAGS_SILENT;
	}

	hr = m_render_client->ReleaseBuffer(frames_available, render_flags);

#else

	// ZeroMemory(p_data, static_cast<SIZE_T>(m_frame_size) * frames_available);
	hr = m_render_client->ReleaseBuffer(frames_available, AUDCLNT_BUFFERFLAGS_SILENT);

#endif

	if (FAILED(hr))
	{
		DebugLogError("Failed to release buffer: 0x%08X.", hr);
		return hr;
	}

	return S_OK;
}

//
// Called when an audio session is disconnected.
HRESULT CKeepSession::OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason)
{
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
