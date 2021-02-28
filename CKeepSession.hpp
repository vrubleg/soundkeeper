#pragma once

#include <mmdeviceapi.h>
#include <audioclient.h>

// Enable Multimedia Class Scheduler Service.
#define ENABLE_MMCSS

enum class KeepStreamType { Silence, Inaudible, Sine };

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
	KeepStreamType          m_stream_type = KeepStreamType::Silence;

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

	enum class SampleType { Unknown, Int16, Int24, Int32, Float32 };
	static SampleType GetSampleType(WAVEFORMATEX* format);
	SampleType              m_mix_sample_type = SampleType::Unknown;
	SampleType              m_out_sample_type = SampleType::Unknown;

	UINT32                  m_sample_rate = 0;
	UINT32                  m_channels_count = 0;
	UINT32                  m_frame_size = 0;

	const UINT32            m_buffer_size_in_ms = 1000;
	UINT32                  m_buffer_size_in_frames = 0;

	// Members for the KeepStreamType::Sine.
	double                  m_frequency = 0.0;
	double                  m_amplitude = 0.0;
	double                  m_theta = 0.0;

	~CKeepSession(void);

public:

	CKeepSession(CSoundKeeper* soundkeeper, IMMDevice* endpoint);
	bool Start();
	void Stop();
	bool IsStarted() const { return m_is_started; }
	bool IsValid() const { return m_curr_mode != RenderingMode::Invalid; };

	void SetStreamType(KeepStreamType stream_type) { m_stream_type = stream_type; }
	KeepStreamType GetStreamType() { return m_stream_type; }

	// Methods for the KeepStreamType::Sine.
	void SetFrequency(double frequency) { m_frequency = frequency; }
	void SetAmplitude(double amplitude) { m_amplitude = amplitude; }
	double GetFrequency() { return m_frequency; }
	double GetAmplitude() { return m_amplitude; }

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
