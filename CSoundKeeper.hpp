#pragma once

#include <mmdeviceapi.h>
#include <audiopolicy.h>

enum class KeepDeviceType { None, Primary, Digital, Analog, All };
enum class KeepStreamType { None, Zero, Fluctuate, Sine, WhiteNoise, BrownNoise, PinkNoise };

class CSoundKeeper;

#include "CSoundSession.hpp"

class CSoundKeeper : public IMMNotificationClient
{
protected:

	LONG                    m_ref_count = 1;
	CriticalSection         m_mutex;

	~CSoundKeeper();

public:

	CSoundKeeper();

	// IUnknown methods

	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface);

	// Callback methods for device-event notifications.

	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR device_id);
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR device_id);
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR device_id);
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR device_id, DWORD new_state);
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR device_id, const PROPERTYKEY key);

protected:

	IMMDeviceEnumerator*    m_dev_enumerator = nullptr;
	bool                    m_is_started = false;
	bool                    m_is_retry_required = false;
	CSoundSession**         m_sessions = nullptr;
	UINT                    m_sessions_count = 0;
	AutoResetEvent          m_do_shutdown = false;
	AutoResetEvent          m_do_restart = false;
	AutoResetEvent          m_do_retry = false;

	bool                    m_cfg_allow_remote = false;
	bool                    m_cfg_no_sleep = false;
	KeepDeviceType          m_cfg_device_type = KeepDeviceType::Primary;
	KeepStreamType          m_cfg_stream_type = KeepStreamType::Zero;
	double                  m_cfg_frequency = 0.0;
	double                  m_cfg_amplitude = 0.0;
	double                  m_cfg_play_seconds = 0.0;
	double                  m_cfg_wait_seconds = 0.0;
	double                  m_cfg_fade_seconds = 0.0;

	HRESULT Start();
	HRESULT Stop();
	HRESULT Restart();
	bool Retry();
	CSoundSession* FindSession(LPCWSTR device_id);

public:

	void SetDeviceType(KeepDeviceType device_type) { m_cfg_device_type = device_type; }
	void SetStreamType(KeepStreamType stream_type) { m_cfg_stream_type = stream_type; }
	void SetAllowRemote(bool allow) { m_cfg_allow_remote = allow; }
	void SetNoSleep(bool value) { m_cfg_no_sleep = value; }
	KeepDeviceType GetDeviceType() const { return m_cfg_device_type; }
	KeepStreamType GetStreamType() const { return m_cfg_stream_type; }
	bool GetAllowRemote() const { return m_cfg_allow_remote; }
	bool GetNoSleep() const { return m_cfg_no_sleep; }

	// Configuration methods.
	void SetFrequency(double frequency) { m_cfg_frequency = frequency; }
	void SetAmplitude(double amplitude) { m_cfg_amplitude = amplitude; }
	void SetPeriodicPlaying(double seconds) { m_cfg_play_seconds = seconds; }
	void SetPeriodicWaiting(double seconds) { m_cfg_wait_seconds = seconds; }
	void SetFading(double seconds) { m_cfg_fade_seconds = seconds; }
	double GetFrequency() const { return m_cfg_frequency; }
	double GetAmplitude() const { return m_cfg_amplitude; }
	double GetPeriodicPlaying() const { return m_cfg_play_seconds; }
	double GetPeriodicWaiting() const { return m_cfg_wait_seconds; }
	double GetFading() const { return m_cfg_fade_seconds; }

	// Set stream type and defaults.
	void SetStreamTypeDefaults(KeepStreamType stream_type);

	void FireRetry();
	void FireRestart();
	void FireShutdown();

	void ParseStreamArgs(KeepStreamType stream_type, const char* args);
	void ParseModeString(const char* args);
	HRESULT Run();
	static HRESULT Main();
};
