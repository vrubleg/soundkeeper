#include "StdAfx.h"
#define INITGUID
#include "CKeepSession.hpp"

// Inaudible tone generation.
// #define ENABLE_INAUDIBLE

// Enable Multimedia Class Scheduler Service
#define ENABLE_MMCSS

#ifdef ENABLE_MMCSS
#include <avrt.h>
#endif

CKeepSession::~CKeepSession(void)
{
	//  Empty destructor - everything should be released in the Shutdown() call
}

CKeepSession::CKeepSession(CSoundKeeper* soundkeeper, IMMDevice* endpoint) :
_RefCount(1), SoundKeeper(soundkeeper), _Endpoint(endpoint), _AudioClient(NULL),
_RenderClient(NULL), _RenderThread(NULL), _ShutdownEvent(NULL), _MixFormat(NULL),
_AudioSessionControl(NULL), BufferSizeInMs(1000)
{
	_Endpoint->AddRef();    // Since we're holding a copy of the endpoint, take a reference to it.  It'll be released in Shutdown();
	SoundKeeper->AddRef();
}

HRESULT STDMETHODCALLTYPE CKeepSession::QueryInterface(REFIID Iid, void **Object)
{
	if (Object == NULL)
	{
		return E_POINTER;
	}
	*Object = NULL;

	if (Iid == IID_IUnknown)
	{
		*Object = static_cast<IUnknown *>(static_cast<IAudioSessionEvents *>(this));
		AddRef();
	}
	else if (Iid == __uuidof(IAudioSessionEvents))
	{
		*Object = static_cast<IAudioSessionEvents *>(this);
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
	return InterlockedIncrement(&_RefCount);
}

ULONG STDMETHODCALLTYPE CKeepSession::Release()
{
	ULONG returnValue = InterlockedDecrement(&_RefCount);
	if (returnValue == 0)
	{
		if (_IsStarted) Shutdown();
		delete this;
	}
	return returnValue;
}

//
// Initialize and start the renderer
bool CKeepSession::Initialize()
{
	if (_IsStarted) return true;

	HRESULT hr = S_OK;
	_IsStarted = false;

	//
	//  Create shutdown event - we want auto reset events that start in the not-signaled state.
	_ShutdownEvent = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	if (_ShutdownEvent == NULL)
	{
		DebugLogError("Unable to create shutdown event: 0x%08X.", GetLastError());
		goto error;
	}

	//
	// Now activate an IAudioClient object on our preferred endpoint and retrieve the mix format for that endpoint
	hr = _Endpoint->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void **>(&_AudioClient));
	if (FAILED(hr))
	{
		DebugLogError("Unable to activate audio client: 0x%08X.", hr);
		goto error;
	}

	//
	// Load the MixFormat. This may differ depending on the shared mode used
	hr = _AudioClient->GetMixFormat(&_MixFormat);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get mix format on audio client: 0x%08X.", hr);
		goto error;
	}
	_FrameSize = _MixFormat->nBlockAlign;

	//
	// Crack open the mix format and determine what kind of samples are being rendered
	if (_MixFormat->wFormatTag == WAVE_FORMAT_PCM
		|| _MixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE *>(_MixFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
	{
		DebugLog("Format: PCM %d-bit integer.", _MixFormat->wBitsPerSample);
		if (_MixFormat->wBitsPerSample == 16)
		{
			_RenderSampleType = SampleTypeInt16;
		}
		else
		{
			DebugLogError("Unsupported PCM integer sample type.");
			goto error;
		}
	}
	else if (_MixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
		|| (_MixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE *>(_MixFormat)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
	{
		DebugLog("Format: PCM %d-bit float.", _MixFormat->wBitsPerSample);
		if (_MixFormat->wBitsPerSample == 32)
		{
			_RenderSampleType = SampleTypeFloat32;
		}
		else
		{
			DebugLogError("Unsupported PCM float sample type.");
			goto error;
		}
	}
	else
	{
		DebugLogError("Unrecognized device format.");
		goto error;
	}

	//
	// Initialize WASAPI in timer driven mode.
	hr = _AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, static_cast<UINT64>(BufferSizeInMs) * 10000, 0, _MixFormat, NULL);

	if (FAILED(hr))
	{
		DebugLogError("Unable to initialize audio client: 0x%08X.", hr);
		goto error;
	}

	//
	// Retrieve the buffer size for the audio client.
	hr = _AudioClient->GetBufferSize(&_BufferSizeInFrames);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get audio client buffer: 0x%08X.", hr);
		goto error;
	}

	hr = _AudioClient->GetService(IID_PPV_ARGS(&_RenderClient));
	if (FAILED(hr))
	{
		DebugLogError("Unable to get new render client: 0x%08X.", hr);
		goto error;
	}

	//
	// Register for session and endpoint change notifications.  
	hr = _AudioClient->GetService(IID_PPV_ARGS(&_AudioSessionControl));
	if (FAILED(hr))
	{
		DebugLogError("Unable to retrieve session control: 0x%08X.", hr);
		goto error;
	}
	hr = _AudioSessionControl->RegisterAudioSessionNotification(this);
	if (FAILED(hr))
	{
		DebugLogError("Unable to register for stream switch notifications: 0x%08X.", hr);
		goto error;
	}

	//
	// We want to pre-roll one buffer's worth of silence into the pipeline. That way the audio engine won't glitch on startup.  
	BYTE *pData;
	hr = _RenderClient->GetBuffer(_BufferSizeInFrames, &pData);
	if (FAILED(hr))
	{
		DebugLogError("Failed to get buffer: 0x%08X.", hr);
		goto error;
	}
	hr = _RenderClient->ReleaseBuffer(_BufferSizeInFrames, AUDCLNT_BUFFERFLAGS_SILENT);
	if (FAILED(hr))
	{
		DebugLogError("Failed to release buffer: 0x%08X.", hr);
		goto error;
	}

	//
	// Now create the thread which is going to drive the renderer
	_RenderThread = CreateThread(NULL, 0, WASAPIRenderThread, this, 0, NULL);
	if (_RenderThread == NULL)
	{
		DebugLogError("Unable to create transport thread: 0x%08X.", GetLastError());
		goto error;
	}

	//
	// We're ready to go, start rendering!
	hr = _AudioClient->Start();
	if (FAILED(hr))
	{
		DebugLogError("Unable to start render client: 0x%08X.", hr);
		goto error;
	}
	_IsStarted = true;

	return true;

error:

	Shutdown();
	return false;
}

bool CKeepSession::IsStarted()
{
	return _IsStarted;
}

//
// Stop the renderer and free all the resources
void CKeepSession::Shutdown()
{
	HRESULT hr;

	if (_ShutdownEvent)
	{
		SetEvent(_ShutdownEvent);
	}

	if (_IsStarted)
	{
		hr = _AudioClient->Stop();
		_IsStarted = false;
		if (FAILED(hr))
		{
			DebugLogError("Unable to stop audio client: 0x%08X.", hr);
		}
	}

	if (_RenderThread)
	{
		WaitForSingleObject(_RenderThread, INFINITE);

		CloseHandle(_RenderThread);
		_RenderThread = NULL;
	}

	if (_ShutdownEvent)
	{
		CloseHandle(_ShutdownEvent);
		_ShutdownEvent = NULL;
	}

	SafeRelease(&_Endpoint);
	SafeRelease(&_AudioClient);
	SafeRelease(&_RenderClient);
	SafeRelease(&SoundKeeper);

	if (_MixFormat)
	{
		CoTaskMemFree(_MixFormat);
		_MixFormat = NULL;
	}

	if (_AudioSessionControl != NULL)
	{
		_AudioSessionControl->UnregisterAudioSessionNotification(this);
		SafeRelease(&_AudioSessionControl);
	}
}

//
//  Render thread - processes samples from the audio engine
//
DWORD CKeepSession::WASAPIRenderThread(LPVOID Context)
{
	CKeepSession *renderer = static_cast<CKeepSession *>(Context);
	return renderer->DoRenderThread();
}

DWORD CKeepSession::DoRenderThread()
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		DebugLogError("Unable to initialize COM in render thread: 0x%08X.", hr);
		return hr;
	}

#ifdef ENABLE_MMCSS
	HANDLE mmcssHandle = NULL;
	DWORD mmcssTaskIndex = 0;
	mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &mmcssTaskIndex);
	if (mmcssHandle == NULL)
	{
		DebugLogError("Unable to enable MMCSS on render thread: 0x%08X.", GetLastError());
	}
#endif

	bool playing = true;
	while (playing) switch (WaitForSingleObject(_ShutdownEvent, BufferSizeInMs / 2 + BufferSizeInMs / 4))
	{
	case WAIT_TIMEOUT: // Timeout - provide the next buffer of samples

		BYTE *pData;
		UINT32 padding;
		UINT32 framesAvailable;

		//  We want to find out how much of the buffer *isn't* available (is padding)
		hr = _AudioClient->GetCurrentPadding(&padding);
		if (FAILED(hr))
		{
			playing = false;
			break;
		}

		//  Calculate the number of frames available
		framesAvailable = _BufferSizeInFrames - padding;
#ifdef ENABLE_INAUDIBLE
		framesAvailable &= 0xFFFFFFFC; // Must be a multiple of 4.
#endif
		if (framesAvailable == 0)
		{
			// It can happen right after waking PC up after sleeping, so just do nothing.
			break;
		}

		hr = _RenderClient->GetBuffer(framesAvailable, &pData);
		if (FAILED(hr))
		{
			playing = false;
			break;
		}

#ifdef ENABLE_INAUDIBLE
		if (_RenderSampleType == SampleTypeInt16)
		{
			UINT32 n = 0;
			constexpr INT16 tbl[] = { -1, 0, 1, 0 };
			for (size_t i = 0; i < framesAvailable; i++)
			{
				for (size_t j = 0; j < _MixFormat->nChannels; j++)
				{
					*reinterpret_cast<INT16*>(pData + j * 2) = tbl[n];
				}
				pData += _FrameSize;
				n = ++n % 4;
			}
		}
		else
		{
			UINT32 n = 0;
			// 0xb8000100 = -3.051851E-5 = -1.0/32767.
			// 0x38000100 =  3.051851E-5 =  1.0/32767.
			constexpr UINT32 tbl[] = { 0xb8000100, 0, 0x38000100, 0 };
			for (size_t i = 0; i < framesAvailable; i++)
			{
				for (size_t j = 0; j < _MixFormat->nChannels; j++)
				{
					*reinterpret_cast<UINT32*>(pData + j * 4) = tbl[n];
				}
				pData += _FrameSize;
				n = ++n % 4;
			}
		}

		hr = _RenderClient->ReleaseBuffer(framesAvailable, NULL);
#else
		// ZeroMemory(pData, static_cast<SIZE_T>(_FrameSize) * framesAvailable);
		hr = _RenderClient->ReleaseBuffer(framesAvailable, AUDCLNT_BUFFERFLAGS_SILENT);
#endif

		if (FAILED(hr))
		{
			playing = false;
			break;
		}
		break;

	case WAIT_OBJECT_0 + 0: // _ShutdownEvent
	default:

		playing = false; // We're done, exit the loop.
		break;
	}

#ifdef ENABLE_MMCSS
	if (mmcssHandle != NULL) AvRevertMmThreadCharacteristics(mmcssHandle);
#endif

	CoUninitialize();
	return 0;
}

//
//  Called when an audio session is disconnected.  
//
//  When a session is disconnected because of a device removal or format change event, we just want 
//  to let the render thread know that the session's gone away
//
HRESULT CKeepSession::OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason)
{
	SetEvent(_ShutdownEvent);
	SoundKeeper->FireRestart();
	return S_OK;
}

HRESULT CKeepSession::OnSimpleVolumeChanged(float NewSimpleVolume, BOOL NewMute, LPCGUID EventContext)
{
	if (NewMute)
	{
		// Shutdown Sound Keeper when muted
		SoundKeeper->FireShutdown();
	}
	return S_OK;
}
