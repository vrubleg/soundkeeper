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

	bool                    m_is_valid = false;

	HANDLE                  m_render_thread = NULL;
	ManualResetEvent        m_is_started = false;
	ManualResetEvent        m_do_stop = true;

	IAudioClient*           m_audio_client = nullptr;
	IAudioRenderClient*     m_render_client = nullptr;
	IAudioSessionControl*   m_audio_session_control = nullptr;

	enum sample_type_t
	{
		k_sample_type_unknown,
		k_sample_type_float32,
		k_sample_type_int16,
	};

	WAVEFORMATEX*           m_mix_format = nullptr;
#if defined(ENABLE_INAUDIBLE) || defined(_DEBUG)
	sample_type_t           m_sample_type = k_sample_type_unknown;
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
	bool IsValid() const { return m_is_valid; };

protected:

	//
	// Rendering thread.
	//

	static DWORD APIENTRY StartRenderingThread(LPVOID Context);
	HRESULT RenderingThread();
	HRESULT RenderingInit();
	HRESULT RenderingLoop();
	void    RenderingFree();
	HRESULT Render();

	//
	// IAudioSessionEvents.
	//

	STDMETHOD(OnDisplayNameChanged) (LPCWSTR /*NewDisplayName*/, LPCGUID /*EventContext*/) { return S_OK; };
	STDMETHOD(OnIconPathChanged) (LPCWSTR /*NewIconPath*/, LPCGUID /*EventContext*/) { return S_OK; };
	STDMETHOD(OnSimpleVolumeChanged) (float /*NewSimpleVolume*/, BOOL /*NewMute*/, LPCGUID /*EventContext*/);
	STDMETHOD(OnChannelVolumeChanged) (DWORD /*ChannelCount*/, float /*NewChannelVolumes*/[], DWORD /*ChangedChannel*/, LPCGUID /*EventContext*/) { return S_OK; };
	STDMETHOD(OnGroupingParamChanged) (LPCGUID /*NewGroupingParam*/, LPCGUID /*EventContext*/) { return S_OK; };
	STDMETHOD(OnStateChanged) (AudioSessionState /*NewState*/) { return S_OK; };
	STDMETHOD(OnSessionDisconnected) (AudioSessionDisconnectReason DisconnectReason);

	//
	// IUnknown
	//

public:

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object);
	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();
};
