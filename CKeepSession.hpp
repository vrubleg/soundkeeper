#pragma once

#include <mmdeviceapi.h>
#include <audioclient.h>

class CKeepSession;
#include "CSoundKeeper.hpp"

//
// Play silence via WASAPI
//

class CKeepSession : IAudioSessionEvents
{
protected:

	LONG                    m_ref_count = 1;

	CSoundKeeper*           m_soundkeeper = nullptr;
	IMMDevice*              m_endpoint = nullptr;

	HANDLE                  m_render_thread = NULL;
	HANDLE                  m_stop_event = NULL;

	IAudioClient*           m_audio_client = nullptr;
	bool                    m_audio_client_is_started = false;
	IAudioRenderClient*     m_render_client = nullptr;
	IAudioSessionControl*   m_audio_session_control = nullptr;

	enum sample_type_t
	{
		k_sample_type_float32,
		k_sample_type_int16,
	};

	WAVEFORMATEX*           m_mix_format = nullptr;
	UINT32                  m_frame_size = 0;
	sample_type_t           m_sample_type = k_sample_type_float32;

	UINT32                  m_buffer_size_in_ms = 500;
	UINT32                  m_buffer_size_in_frames = 0;

	~CKeepSession(void);

public:

	CKeepSession(CSoundKeeper* soundkeeper, IMMDevice* endpoint);
	bool Start();
	bool IsStarted();
	void Stop();

protected:

	//
	// Rendering thread.
	//

	static DWORD APIENTRY StartRenderingThread(LPVOID Context);
	HRESULT RenderingThread();
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
