#include "StdAfx.h"
#include "CSoundKeeper.h"

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

	IMMDeviceCollection *DevCollection = NULL;
	hr = DevEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &DevCollection);
	if (FAILED(hr))
	{
		printf("Unable to retrieve device collection: %x\n", hr);
		goto Exit;
	}

	hr = DevCollection->GetCount(&KeepersCount);
	if (FAILED(hr))
	{
		printf("Unable to get device collection length: %x\n", hr);
		goto Exit;
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
		Keepers[i]->Initialize();
		SafeRelease(&device);
	}

	IsStarted = true;

Exit:

	SafeRelease(&DevCollection);
	return hr;
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
	HRESULT result = S_OK;
	HRESULT hr = S_OK;

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&DevEnumerator));
	if (FAILED(hr))
	{
		printf("Unable to instantiate device enumerator: %x\n", hr);
		result = hr;
		goto Exit;
	}

	ShutdownEvent = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	if (ShutdownEvent == NULL)
	{
		printf("Unable to create shutdown event: %d.\n", GetLastError());
		return false;
	}

	RestartEvent = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	if (RestartEvent == NULL)
	{
		printf("Unable to create restart event: %d.\n", GetLastError());
		return false;
	}

	hr = DevEnumerator->RegisterEndpointNotificationCallback(this);
	if (FAILED(hr))
	{
		printf("Unable to register for stream switch notifications: %x\n", hr);
		return false;
	}

	// Main loop
	Start();
	HANDLE wait[2] = { RestartEvent, ShutdownEvent };
	bool working = true;
	while (working)
	{
		switch (WaitForMultipleObjects(2, wait, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0 + 0:
			// Prevent multiple restarts
			while (WaitForSingleObject(RestartEvent, 750) != WAIT_TIMEOUT)
			{
				Sleep(1000);
			}
			Restart();
			break;

		case WAIT_OBJECT_0 + 1:
		default:
			// Shutdown
			working = false; // We're done, exit the loop
			break;
		}
	}
	Stop();

Exit:

	if (ShutdownEvent)
	{
		CloseHandle(ShutdownEvent);
		ShutdownEvent = NULL;
	}
	if (RestartEvent)
	{
		CloseHandle(ShutdownEvent);
		ShutdownEvent = NULL;
	}
	if (DevEnumerator)
	{
		hr = DevEnumerator->UnregisterEndpointNotificationCallback(this);
		if (FAILED(hr))
		{
			printf("Unable to unregister for endpoint notifications: %x\n", hr);
		}
	}
	SafeRelease(&DevEnumerator);

	return result;
}
