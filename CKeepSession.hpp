#pragma once

#include <mmdeviceapi.h>
#include <audioclient.h>

// Inaudible tone generation.
// #define ENABLE_INAUDIBLE

// Enable Multimedia Class Scheduler Service.
#define ENABLE_MMCSS

class CKeepSession;
#include "CSoundKeeper.hpp"

//
// Play silence via WASAPI
//

class CKeepSession : IAudioSessionEvents
{
protected:

	LONG                    m_ref_count = 1;
	CriticalSection         m_mutex;

	CSoundKeeper*           m_soundkeeper = nullptr;
	IMMDevice*              m_endpoint = nullptr;

	HANDLE                  m_render_thread = NULL;
	ManualResetEvent        m_is_started = false;

	enum class RenderingMode { Stop, Render, Retry, WaitExclusive, Invalid };
	RenderingMode           m_curr_mode = RenderingMode::Stop;
	RenderingMode           m_next_mode = RenderingMode::Stop;
	AutoResetEvent          m_interrupt = false;
	DWORD                   m_play_attempts = 0;
	DWORD                   m_wait_attempts = 0;

	void DeferNextMode(RenderingMode next_mode)
	{
		m_next_mode = next_mode;
		m_interrupt = true;
	}

	IAudioClient*           m_audio_client = nullptr;
	IAudioRenderClient*     m_render_client = nullptr;
	IAudioSessionControl*   m_audio_session_control = nullptr;

	WAVEFORMATEX*           m_mix_format = nullptr;
#if defined(ENABLE_INAUDIBLE) || defined(_DEBUG)
	enum class SampleType { Unknown, Float32, Int16 };
	SampleType              m_sample_type = SampleType::Unknown;
	UINT32                  m_channels_count = 0;
	UINT32                  m_frame_size = 0;
#endif

	UINT32                  m_buffer_size_in_ms = 1000;
	UINT32                  m_buffer_size_in_frames = 0;

	~CKeepSession(void);

public:

	CKeepSession(CSoundKeeper* soundkeeper, IMMDevice* endpoint);
	bool Start();
	void Stop();
	bool IsStarted() const { return m_is_started; }
	bool IsValid() const { return m_curr_mode != RenderingMode::Invalid; };

protected:

	//
	// Rendering thread.
	//

	static DWORD APIENTRY StartRenderingThread(LPVOID Context);
	DWORD RenderingThread();
	RenderingMode Rendering();
	HRESULT Render();
	RenderingMode WaitExclusive();

	//
	// IAudioSessionEvents.
	//

	STDMETHOD(OnDisplayNameChanged) (LPCWSTR /*NewDisplayName*/, LPCGUID /*EventContext*/) { return S_OK; };
	STDMETHOD(OnIconPathChanged) (LPCWSTR /*NewIconPath*/, LPCGUID /*EventContext*/) { return S_OK; };
	STDMETHOD(OnSimpleVolumeChanged) (float /*NewSimpleVolume*/, BOOL /*NewMute*/, LPCGUID /*EventContext*/);
	STDMETHOD(OnChannelVolumeChanged) (DWORD /*ChannelCount*/, float /*NewChannelVolumes*/[], DWORD /*ChangedChannel*/, LPCGUID /*EventContext*/) { return S_OK; };
	STDMETHOD(OnGroupingParamChanged) (LPCGUID /*NewGroupingParam*/, LPCGUID /*EventContext*/) { return S_OK; };
	STDMETHOD(OnStateChanged) (AudioSessionState NewState);
	STDMETHOD(OnSessionDisconnected) (AudioSessionDisconnectReason DisconnectReason);

	//
	// IUnknown
	//

public:

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object);
	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();
};
