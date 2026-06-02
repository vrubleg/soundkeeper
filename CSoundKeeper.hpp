#pragma once

#include "Common.hpp"

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
	atomic_bool             m_is_suspended = false;
	atomic_bool             m_is_display_off = false;
	atomic_bool             m_is_user_locked = false;
	CSoundSession**         m_sessions = nullptr;
	UINT                    m_sessions_count = 0;
	AutoResetEvent          m_do_shutdown = false;
	AutoResetEvent          m_do_stop = false;
	AutoResetEvent          m_do_start = false;

	bool                    m_cfg_allow_remote = false;
	bool                    m_cfg_sleep_with_idle_timer = true;
	bool                    m_cfg_sleep_with_system = true;
	bool                    m_cfg_sleep_with_display = false;
	bool                    m_cfg_sleep_with_user_lock = false;
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
	CSoundSession* FindSession(LPCWSTR device_id);

	static ULONG CALLBACK SuspendResumeCallbackEntry(PVOID Context, ULONG Type, PVOID Setting);
	ULONG SuspendResumeCallback(ULONG Type);
	static ULONG CALLBACK DisplayStateCallbackEntry(PVOID Context, ULONG Type, PVOID Setting);
	ULONG DisplayStateCallback(MONITOR_DISPLAY_STATE state);
	static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	VOID UserSessionStateCallback(WPARAM wParam, LPARAM lParam);

public:

	void SetDeviceType(KeepDeviceType device_type) { m_cfg_device_type = device_type; }
	void SetStreamType(KeepStreamType stream_type) { m_cfg_stream_type = stream_type; }
	void SetAllowRemote(bool allow) { m_cfg_allow_remote = allow; }
	void SetSleepWithIdleTimer(bool value) { m_cfg_sleep_with_idle_timer = value; }
	void SetSleepWithSystem(bool value) { m_cfg_sleep_with_system = value; }
	void SetSleepWithDisplay(bool value) { m_cfg_sleep_with_display = value; }
	void SetSleepWithUserLock(bool value) { m_cfg_sleep_with_user_lock = value; }
	KeepDeviceType GetDeviceType() const { return m_cfg_device_type; }
	KeepStreamType GetStreamType() const { return m_cfg_stream_type; }
	bool GetAllowRemote() const { return m_cfg_allow_remote; }
	bool GetSleepWithIdleTimer() const { return m_cfg_sleep_with_idle_timer; }
	bool GetSleepWithSystem() const { return m_cfg_sleep_with_system; }
	bool GetSleepWithDisplay() const { return m_cfg_sleep_with_display; }
	bool GetSleepWithUserLock() const { return m_cfg_sleep_with_user_lock; }

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

	bool HasReasonsToSleep();
	void FireStop();
	void FireStart();
	void FireRestart();
	void FireShutdown();

	void ParseStreamArgs(KeepStreamType stream_type, const char* args);
	void ParseModeString(const char* args);
	HRESULT Main();
	static HRESULT MainEntry();
};
