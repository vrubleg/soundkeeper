#pragma once

#include <mmdeviceapi.h>
#include <audiopolicy.h>

class CSoundKeeper;
#include "CKeepSession.hpp"

enum class KeepDeviceType { Primary, Digital, Analog, All };

class CSoundKeeper : public IMMNotificationClient
{
protected:

	LONG m_ref_count = 1;

	~CSoundKeeper();

public:

	CSoundKeeper();

	// IUnknown methods

	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID **ppvInterface);

	// Callback methods for device-event notifications.

	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId);
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId);
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId);
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState);
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key);

protected:

	IMMDeviceEnumerator*    m_dev_enumerator = nullptr;
	bool                    m_is_started = false;
	bool                    m_is_retry_required = false;
	CKeepSession**          m_sessions = nullptr;
	UINT                    m_sessions_count = 0;
	AutoResetEvent          m_do_shutdown = false;
	AutoResetEvent          m_do_restart = false;
	AutoResetEvent          m_do_retry = false;

	KeepDeviceType          m_cfg_device_type = KeepDeviceType::Primary;
	KeepStreamType          m_cfg_stream_type = KeepStreamType::Zero;

	// Members for the KeepStreamType::Sine.
	double                  m_cfg_frequency = 0.0;
	double                  m_cfg_amplitude = 0.0;

	HRESULT Start();
	HRESULT Stop();
	HRESULT Restart();
	bool Retry();

public:

	void SetDeviceType(KeepDeviceType device_type) { m_cfg_device_type = device_type; }
	void SetStreamType(KeepStreamType stream_type) { m_cfg_stream_type = stream_type; }
	KeepDeviceType GetDeviceType() { return m_cfg_device_type; }
	KeepStreamType GetStreamType() { return m_cfg_stream_type; }

	// Methods for the KeepStreamType::Sine.
	void SetFrequency(double frequency) { m_cfg_frequency = frequency; }
	void SetAmplitude(double amplitude) { m_cfg_amplitude = amplitude; }
	double GetFrequency() { return m_cfg_frequency; }
	double GetAmplitude() { return m_cfg_amplitude; }

	void FireRetry();
	void FireRestart();
	void FireShutdown();
	HRESULT Main();
};
