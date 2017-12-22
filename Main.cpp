// ---------------------------------------------------------------------------------------------------------------------
// Digital Sound Keeper v1.0.1 [2017/12/21]
// Prevents SPDIF/HDMI digital playback devices from falling asleep. Uses WASAPI, requires Windows 7+.
// (C) 2014-2017 Evgeny Vrublevsky <me@veg.by>
// ---------------------------------------------------------------------------------------------------------------------

#include "StdAfx.h"
#include "CSoundKeeper.h"

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	// Prevent from multiple instances (the mutex will be destroyed automatically on program exit)
	CreateMutexA(NULL, FALSE, "DigitalSoundKeeper");
	if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED)
	{
		MessageBoxA(0, "The program is already running", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
		return 1;
	}

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED); // A GUI application should use COINIT_APARTMENTTHREADED
	if (FAILED(hr))
	{
		MessageBoxA(0, "Cannot initialize COM", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
		return hr;
	}

	CSoundKeeper *keeper = new CSoundKeeper();
	hr = keeper->Main();
	if (FAILED(hr))
	{
		MessageBoxA(0, "Cannot run main code", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
	}
	keeper->Release(); // Destroys the object

	CoUninitialize();
	return hr;
}
