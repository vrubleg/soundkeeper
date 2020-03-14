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

	LONG                    m_ref_count;

	CSoundKeeper*           m_soundkeeper;
	IMMDevice*              m_endpoint;

	HANDLE                  m_render_thread;
	HANDLE                  m_shutdown_event;

	IAudioClient*           m_audio_client;
	bool                    m_audio_client_is_started = false;
	IAudioRenderClient*     m_render_client;
	IAudioSessionControl*   m_audio_session_control;

	enum RenderSampleType
	{
		SampleTypeFloat32,
		SampleTypeInt16,
	};

	WAVEFORMATEX*           m_mix_format;
	UINT32                  m_frame_size = 0;
	RenderSampleType        m_sample_type = SampleTypeFloat32;

	UINT32                  m_buffer_size_in_ms;
	UINT32                  m_buffer_size_in_frames = 0;

	~CKeepSession(void);

public:

	CKeepSession(CSoundKeeper* soundkeeper, IMMDevice* endpoint);
	bool Initialize();
	bool IsStarted();
	void Shutdown();

private:

	//
	//  Render buffer management.
	//
	static DWORD __stdcall WASAPIRenderThread(LPVOID Context);
	DWORD CKeepSession::DoRenderThread();

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

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Iid, void **Object);
	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();
};
