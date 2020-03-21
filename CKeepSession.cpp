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
	m_is_valid = true;
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
	if (!m_is_valid) return false;
	if (m_is_started) return true;

	this->Stop();
	m_do_stop = false;

	//
	// Now create the thread which is going to drive the renderer.
	m_render_thread = CreateThread(NULL, 0, StartRenderingThread, this, 0, NULL);
	if (m_render_thread == NULL)
	{
		DebugLogError("Unable to create rendering thread: 0x%08X.", GetLastError());
		goto error;
	}

	// Wait until rendering is started.
	HANDLE wait_handles[] = { m_is_started, m_render_thread };
	if (WaitForMultipleObjects(_countof(wait_handles), wait_handles, FALSE, INFINITE) != WAIT_OBJECT_0)
	{
		DebugLogError("Unable to start rendering.");
		goto error;
	}

	return true;

error:

	Stop();
	return false;
}

//
// Stop the renderer and free all the resources.
void CKeepSession::Stop()
{
	if (!m_audio_client) return;

	m_do_stop = true;

	if (m_render_thread)
	{
		WaitForSingleObject(m_render_thread, INFINITE);
		CloseHandle(m_render_thread);
		m_render_thread = NULL;
	}
}

//
// Rendering thread.
//

DWORD APIENTRY CKeepSession::StartRenderingThread(LPVOID context)
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		DebugLogError("Unable to initialize COM in render thread: 0x%08X.", hr);
		return hr;
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
	hr = renderer->RenderingThread();

#ifdef ENABLE_MMCSS
	if (mmcss_handle != NULL) AvRevertMmThreadCharacteristics(mmcss_handle);
#endif

	CoUninitialize();
	return hr;
}

HRESULT CKeepSession::RenderingThread()
{
	HRESULT hr = S_OK;

	hr = this->RenderingInit();
	if (SUCCEEDED(hr))
	{
		hr = this->RenderingLoop();
	}
	this->RenderingFree();

	return hr;
}

HRESULT CKeepSession::RenderingInit()
{
	HRESULT hr = S_OK;

	//
	// Now activate an IAudioClient object on our preferred endpoint.
	hr = m_endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void **>(&m_audio_client));
	if (FAILED(hr))
	{
		m_is_valid = false;
		DebugLogError("Unable to activate audio client: 0x%08X.", hr);
		return hr;
	}

	//
	// Load the MixFormat. This may differ depending on the shared mode used.
	hr = m_audio_client->GetMixFormat(&m_mix_format);
	if (FAILED(hr))
	{
		m_is_valid = false;
		DebugLogError("Unable to get mix format on audio client: 0x%08X.", hr);
		return hr;
	}

#ifdef ENABLE_INAUDIBLE

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
		if (hr != AUDCLNT_E_DEVICE_IN_USE) { m_is_valid = false; }
		DebugLogError("Unable to initialize audio client: 0x%08X.", hr);
		return hr;
	}

	//
	// Retrieve the buffer size for the audio client.
	hr = m_audio_client->GetBufferSize(&m_buffer_size_in_frames);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get audio client buffer: 0x%08X.", hr);
		return hr;
	}

	hr = m_audio_client->GetService(IID_PPV_ARGS(&m_render_client));
	if (FAILED(hr))
	{
		DebugLogError("Unable to get new render client: 0x%08X.", hr);
		return hr;
	}

	//
	// Register for session and endpoint change notifications.  
	hr = m_audio_client->GetService(IID_PPV_ARGS(&m_audio_session_control));
	if (FAILED(hr))
	{
		DebugLogError("Unable to retrieve session control: 0x%08X.", hr);
		return hr;
	}
	hr = m_audio_session_control->RegisterAudioSessionNotification(this);
	if (FAILED(hr))
	{
		DebugLogError("Unable to register for stream switch notifications: 0x%08X.", hr);
		return hr;
	}

	return S_OK;
}

void CKeepSession::RenderingFree()
{
	if (m_audio_session_control)
	{
		m_audio_session_control->UnregisterAudioSessionNotification(this);
		SafeRelease(&m_audio_session_control);
	}

	SafeRelease(&m_audio_client);
	SafeRelease(&m_render_client);

	if (m_mix_format)
	{
		CoTaskMemFree(m_mix_format);
		m_mix_format = NULL;
	}
}

HRESULT CKeepSession::RenderingLoop()
{
	HRESULT hr = S_OK;

	// We want to pre-roll one buffer of data into the pipeline. That way the audio engine won't glitch on startup.  
	hr = this->Render();
	if (FAILED(hr))
	{
		DebugLogError("Can't render initial buffer: 0x%08X.", hr);
		return hr;
	}

	hr = m_audio_client->Start();
	if (FAILED(hr))
	{
		DebugLogError("Unable to start render client: 0x%08X.", hr);
		return hr;
	}

	m_is_started = true;

	while (true) switch (WaitForSingleObject(m_do_stop, m_buffer_size_in_ms / 2 + m_buffer_size_in_ms / 4))
	{
	case WAIT_TIMEOUT: // Timeout.

		// Provide the next buffer of samples.
		hr = this->Render();
		if (FAILED(hr))
		{
			goto stop;
		}
		break;

	case WAIT_OBJECT_0 + 0: // m_do_stop
	default:

		// We're done, exit the loop.
		goto stop;
	}

stop:

	m_is_started = false;
	m_audio_client->Stop();
	return hr;
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
#ifdef ENABLE_INAUDIBLE
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

#ifdef ENABLE_INAUDIBLE

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
	this->Stop();

	if (DisconnectReason == DisconnectReasonFormatChanged || DisconnectReason == DisconnectReasonExclusiveModeOverride)
	{
		DebugLog("Session is disconnected with reason %d. Retry.", DisconnectReason);
		m_soundkeeper->FireRetry();
	}
	else
 	{
		DebugLog("Session is disconnected with reason %d. Restart.", DisconnectReason);
 		m_is_valid = false;
		m_soundkeeper->FireRestart();
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
