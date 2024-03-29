#pragma once

#include "Common.hpp"

#include <mmdeviceapi.h>
#include <audioclient.h>

class CSoundSession;

#include "CSoundKeeper.hpp"

class CSoundSession : IAudioSessionEvents
{
protected:

	static bool g_is_leaky_wasapi;

public:

	static void EnableWaitExclusiveWorkaround(bool enable) { g_is_leaky_wasapi = enable; }

protected:

	LONG                    m_ref_count = 1;
	CriticalSection         m_mutex;

	CSoundKeeper*           m_soundkeeper = nullptr;
	IMMDevice*              m_endpoint = nullptr;
	LPWSTR                  m_device_id = nullptr;
	KeepStreamType          m_stream_type = KeepStreamType::Zero;

	HANDLE                  m_render_thread = NULL;
	ManualResetEvent        m_is_started = false;

	enum class RenderingMode { Stop, Rendering, Retry, WaitExclusive, TryOpenDevice, Invalid };
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
	static SampleType ParseSampleType(WAVEFORMATEX* format);
	SampleType              m_mix_sample_type = SampleType::Unknown;
	SampleType              m_out_sample_type = SampleType::Unknown;

	UINT32                  m_sample_rate = 0;
	UINT32                  m_channels_count = 0;
	UINT32                  m_frame_size = 0;

	UINT32                  m_buffer_size_in_ms = 1000;
	UINT32                  m_buffer_size_in_frames = 0;

	// Sound generation settings.
	double                  m_frequency = 0.0;
	double                  m_amplitude = 0.0;

	// Periodicity settings.
	double                  m_play_seconds = 0.0;
	double                  m_wait_seconds = 0.0;
	double                  m_fade_seconds = 0.0;

	// Current state.
	uint64_t                m_curr_frame = 0;
	union
	{
		double              m_curr_state[8]{0}; // Pink Noise.
		double              m_curr_value;       // Brown Noise.
		double              m_curr_theta;       // Sine.
	};

public:

	CSoundSession(CSoundKeeper* soundkeeper, IMMDevice* endpoint);
	bool Start();
	void Stop();
	bool IsStarted() const { return m_is_started; }
	bool IsValid() const { return m_curr_mode != RenderingMode::Invalid; };
	LPCWSTR GetDeviceId() const { return m_device_id; }
	DWORD GetDeviceState() { DWORD state = 0; m_endpoint->GetState(&state); return state; }

	void ResetCurrent()
	{
		if (m_curr_frame)
		{
			m_curr_frame = 0;
			memset(m_curr_state, 0, sizeof(m_curr_state));
		}
	}

	void SetStreamType(KeepStreamType stream_type)
	{
		m_stream_type = stream_type;
		this->ResetCurrent();
	}

	KeepStreamType GetStreamType() const
	{
		return m_stream_type;
	}

	// Sine generation settings.

	void SetFrequency(double frequency)
	{
		m_frequency = frequency > 0 ? frequency : 0;
		this->ResetCurrent();
	}

	double GetFrequency() const
	{
		return m_frequency;
	}

	void SetAmplitude(double amplitude)
	{
		m_amplitude = amplitude > 0 ? amplitude : 0;
		this->ResetCurrent();
	}

	double GetAmplitude() const
	{
		return m_amplitude;
	}

	// Periodicity settings.

	void SetPeriodicPlaying(double seconds)
	{
		m_play_seconds = seconds > 0 ? seconds : 0;
		this->ResetCurrent();
	}

	double GetPeriodicPlaying() const
	{
		return m_play_seconds;
	}

	void SetPeriodicWaiting(double seconds)
	{
		m_wait_seconds = seconds > 0 ? seconds : 0;
		this->ResetCurrent();
	}

	double GetPeriodicWaiting() const
	{
		return m_wait_seconds;
	}

	void SetFading(double seconds)
	{
		m_fade_seconds = seconds > 0 ? seconds : 0;
		this->ResetCurrent();
	}

	double GetFading() const
	{
		return m_fade_seconds;
	}

protected:

	~CSoundSession(void);

	//
	// Rendering thread.
	//

	static DWORD APIENTRY StartRenderingThread(LPVOID context);
	DWORD RenderingThread();
	RenderingMode TryOpenDevice();
	RenderingMode Rendering();
	HRESULT Render();
	RenderingMode WaitExclusive();

public:

	//
	// IUnknown
	//

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object);
	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();

protected:

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
};
