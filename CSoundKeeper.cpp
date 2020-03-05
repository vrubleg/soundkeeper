#include "StdAfx.h"
#include "CSoundKeeper.hpp"

CSoundKeeper::CSoundKeeper()
	: _cRef(1), DevEnumerator(NULL), IsStarted(false), Keepers(NULL), KeepersCount(0), ShutdownEvent(NULL), RestartEvent(NULL)
{ }
CSoundKeeper::~CSoundKeeper() { }

// IUnknown methods

ULONG STDMETHODCALLTYPE CSoundKeeper::AddRef()
{
	return InterlockedIncrement(&_cRef);
}

ULONG STDMETHODCALLTYPE CSoundKeeper::Release()
{
	ULONG ulRef = InterlockedDecrement(&_cRef);
	if (0 == ulRef)
	{
		delete this;
	}
	return ulRef;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::QueryInterface(REFIID riid, VOID **ppvInterface)
{
	if (IID_IUnknown == riid)
	{
		AddRef();
		*ppvInterface = (IUnknown*)this;
	}
	else if (__uuidof(IMMNotificationClient) == riid)
	{
		AddRef();
		*ppvInterface = (IMMNotificationClient*)this;
	}
	else
	{
		*ppvInterface = NULL;
		return E_NOINTERFACE;
	}
	return S_OK;
}

// Callback methods for device-event notifications.

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceAdded(LPCWSTR pwstrDeviceId)
{
	FireRestart();
	return S_OK;
};

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
	FireRestart();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState)
{
	FireRestart();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CSoundKeeper::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key)
{
	return S_OK;
}

HRESULT CSoundKeeper::Start()
{
	if (IsStarted) return S_OK;
	HRESULT hr = S_OK;
	IsRetryRequired = false;

	IMMDeviceCollection *DevCollection = NULL;
	hr = DevEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &DevCollection);
	if (FAILED(hr))
	{
		DebugLogError("Unable to retrieve device collection: 0x%08X.", hr);
		goto exit;
	}

	hr = DevCollection->GetCount(&KeepersCount);
	if (FAILED(hr))
	{
		DebugLogError("Unable to get device collection length: 0x%08X.", hr);
		goto exit;
	}

	Keepers = new CKeepSession*[KeepersCount]();

	for (UINT i = 0; i < KeepersCount; i += 1)
	{
		Keepers[i] = nullptr;

		IMMDevice *device;
		DevCollection->Item(i, &device);
		IPropertyStore *properties;
		hr = device->OpenPropertyStore(STGM_READ, &properties);

		PROPVARIANT formfactor;
		PropVariantInit(&formfactor);
		hr = properties->GetValue(PKEY_AudioEndpoint_FormFactor, &formfactor);
		SafeRelease(&properties);
		if (FAILED(hr) || formfactor.vt != VT_UI4 || formfactor.uintVal != SPDIF && formfactor.uintVal != HDMI)
		{
			PropVariantClear(&formfactor);
			SafeRelease(&device);
			continue;
		}
		PropVariantClear(&formfactor);

		Keepers[i] = new CKeepSession(this, device);
		SafeRelease(&device);

		if (!Keepers[i]->Initialize())
		{
			IsRetryRequired = true;
		}
	}

	IsStarted = true;

exit:

	SafeRelease(&DevCollection);
	return hr;
}

bool CSoundKeeper::Retry()
{
	if (!IsRetryRequired) return true;
	IsRetryRequired = false;

	if (Keepers != nullptr)
	{
		for (UINT i = 0; i < KeepersCount; i++)
		{
			if (Keepers[i] != nullptr)
			{
				if (!Keepers[i]->Initialize())
				{
					IsRetryRequired = true;
				}
			}
		}
	}

	return !IsRetryRequired;
}

HRESULT CSoundKeeper::Stop()
{
	if (!IsStarted) return S_OK;

	if (Keepers != nullptr)
	{
		for (UINT i = 0; i < KeepersCount; i++)
		{
			if (Keepers[i] != nullptr)
			{
				Keepers[i]->Shutdown();
				Keepers[i]->Release();
			}
		}
		delete Keepers;
	}

	Keepers = nullptr;
	KeepersCount = 0;
	IsStarted = false;
	return S_OK;
}

HRESULT CSoundKeeper::Restart()
{
	HRESULT hr = S_OK;

	hr = Stop();
	if (FAILED(hr))
	{
		return hr;
	}

	hr = Start();
	if (FAILED(hr))
	{
		return hr;
	}

	return S_OK;
}

void CSoundKeeper::FireRestart()
{
	SetEvent(RestartEvent);
}

void CSoundKeeper::FireShutdown()
{
	SetEvent(ShutdownEvent);
}

HRESULT CSoundKeeper::Main()
{
	HRESULT hr = S_OK;

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&DevEnumerator));
	if (FAILED(hr))
	{
		DebugLogError("Unable to instantiate device enumerator: 0x%08X.", hr);
		goto exit;
	}

	ShutdownEvent = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	if (ShutdownEvent == NULL)
	{
		DebugLogError("Unable to create shutdown event: 0x%08X.", GetLastError());
		hr = E_FAIL;
		goto exit;
	}

	RestartEvent = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	if (RestartEvent == NULL)
	{
		DebugLogError("Unable to create restart event: 0x%08X.", GetLastError());
		hr = E_FAIL;
		goto exit;
	}

	hr = DevEnumerator->RegisterEndpointNotificationCallback(this);
	if (FAILED(hr))
	{
		DebugLogError("Unable to register for stream switch notifications: 0x%08X.", hr);
		goto exit;
	}

	// Main loop
	DebugLog("Start");
	Start();
	HANDLE wait[2] = { RestartEvent, ShutdownEvent };
	bool working = true;
	while (working)
	{
		switch (WaitForMultipleObjects(2, wait, FALSE, IsRetryRequired ? 500 : INFINITE))
		{
		case WAIT_TIMEOUT:
			DebugLog("Retry");
			Retry();
			break;

		case WAIT_OBJECT_0 + 0:
			DebugLog("Restart");
			// Prevent multiple restarts
			while (WaitForSingleObject(RestartEvent, 750) != WAIT_TIMEOUT)
			{
				Sleep(1000);
			}
			Restart();
			break;

		case WAIT_OBJECT_0 + 1:
		default:
			DebugLog("Shutdown");
			// Shutdown
			working = false; // We're done, exit the loop
			break;
		}
	}
	Stop();

exit:

	if (ShutdownEvent)
	{
		CloseHandle(ShutdownEvent);
		ShutdownEvent = NULL;
	}
	if (RestartEvent)
	{
		CloseHandle(RestartEvent);
		RestartEvent = NULL;
	}
	if (DevEnumerator)
	{
		DevEnumerator->UnregisterEndpointNotificationCallback(this);
	}
	SafeRelease(&DevEnumerator);

	return hr;
}
