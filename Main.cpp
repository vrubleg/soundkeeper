// ---------------------------------------------------------------------------------------------------------------------
// Digital Sound Keeper v1.1.0 [2020/07/18]
// Prevents SPDIF/HDMI digital playback devices from falling asleep. Uses WASAPI, requires Windows 7+.
// (C) 2014-2020 Evgeny Vrublevsky <me@veg.by>
// ---------------------------------------------------------------------------------------------------------------------

#include "StdAfx.h"
#include "CSoundKeeper.hpp"

__forceinline int Main()
{
	// Prevent from multiple instances (the mutex will be destroyed automatically on program exit)
	CreateMutexA(NULL, FALSE, "DigitalSoundKeeper");
	if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED)
	{
		MessageBoxA(0, "The program is already running.", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
		return 1;
	}

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED); // A GUI application should use COINIT_APARTMENTTHREADED
	if (FAILED(hr))
	{
		MessageBoxA(0, "Cannot initialize COM.", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
		return hr;
	}

	CSoundKeeper* keeper = new CSoundKeeper();
	keeper->SetDeviceType(KeepDeviceType::Primary);
	keeper->SetStreamType(KeepStreamType::Inaudible);

	char fn_buffer[MAX_PATH];
	DWORD fn_size = GetModuleFileNameA(NULL, fn_buffer, MAX_PATH);
	if (fn_size != 0 && fn_size != MAX_PATH)
	{
		char* filename = strrchr(fn_buffer, '\\');
		if (filename)
		{
			filename++;
			_strlwr(filename);
			if (strstr(filename, "all"))     { keeper->SetDeviceType(KeepDeviceType::All); }
			if (strstr(filename, "digital")) { keeper->SetDeviceType(KeepDeviceType::Digital); }
			if (strstr(filename, "zero"))    { keeper->SetStreamType(KeepStreamType::Silence); }
		}
	}

	hr = keeper->Main();
	if (FAILED(hr))
	{
		MessageBoxA(0, "Cannot run main code.", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
	}
	keeper->Release(); // Destroys the object

	CoUninitialize();
	return hr;
}

#ifdef _DEBUG

int main()
{
	return Main();
}

#else

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	return Main();
}

#endif
