// ---------------------------------------------------------------------------------------------------------------------
// Digital Sound Keeper v1.0 [24.12.2014]
// Prevents SPDIF/HDMI digital playback devices from falling asleep. Uses WASAPI, requires Windows 7+.
// (C) 2014 Evgeny Vrublevsky <veg@tut.by>
// ---------------------------------------------------------------------------------------------------------------------

#include "StdAfx.h"
#include "CSoundKeeper.h"

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	// Prevent from multiple instances
	HANDLE mutex = CreateMutex(NULL, FALSE, L"DigitalSoundKeeper");
	if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED) return 1;

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED); // A GUI application should use COINIT_APARTMENTTHREADED
	if (FAILED(hr))
	{
		MessageBoxA(0, "Unable to initialize COM", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
		return 1;
	}

	CSoundKeeper *keeper = new CSoundKeeper();
	keeper->Main();
	keeper->Release();

	CoUninitialize();
	return 0;
}
