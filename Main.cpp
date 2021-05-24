// ---------------------------------------------------------------------------------------------------------------------
// Digital Sound Keeper v1.1.0 [2020/07/18]
// Prevents SPDIF/HDMI digital playback devices from falling asleep. Uses WASAPI, requires Windows 7+.
// (C) 2014-2020 Evgeny Vrublevsky <me@veg.by>
// ---------------------------------------------------------------------------------------------------------------------

#include "Common.hpp"
#include "CSoundKeeper.hpp"

void ParseMode(CSoundKeeper* keeper, const char* args)
{
	char buf[MAX_PATH];
	strcpy_s(buf, args);
	_strlwr(buf);

	if (strstr(buf, "all"))     { keeper->SetDeviceType(KeepDeviceType::All); }
	if (strstr(buf, "digital")) { keeper->SetDeviceType(KeepDeviceType::Digital); }

	if (strstr(buf, "zero") || strstr(buf, "null"))
	{
		keeper->SetStreamType(KeepStreamType::Silence);
	}
	else if (char* p = strstr(buf, "sine"))
	{
		keeper->SetStreamType(KeepStreamType::Sine);
		keeper->SetFrequency(1.00);
		keeper->SetAmplitude(0.01);

		// Parse arguments.
		p += 4;
		while (*p)
		{
			if (*p == ' ' || *p == '-') { p++; }
			else if (*p == 'f' || *p == 'a')
			{
				char type = *p;
				p++;
				while (*p == ' ' || *p == '=') { p++; }
				double value = fabs(strtod(p, &p));
				if (type == 'f')
				{
					keeper->SetFrequency(std::min(value, 96000.0));
				}
				else
				{
					keeper->SetAmplitude(std::min(value / 100.0, 1.0));
				}
			}
			else
			{
				break;
			}
		}
	}
}

__forceinline int Main()
{
	// Prevent from multiple instances (the mutex will be destroyed automatically on program exit)
	CreateMutexA(NULL, FALSE, "DigitalSoundKeeper");
	if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED)
	{
#ifndef _DEBUG
		MessageBoxA(0, "The program is already running.", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
#else
		DebugLogError("The program is already running.");
#endif
		return 1;
	}

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED); // A GUI application should use COINIT_APARTMENTTHREADED
	if (FAILED(hr))
	{
#ifndef _DEBUG
		MessageBoxA(0, "Cannot initialize COM.", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
#else
		DebugLogError("Cannot initialize COM: 0x%08X.", hr);
#endif
		return hr;
	}

	CSoundKeeper* keeper = new CSoundKeeper();
	keeper->SetDeviceType(KeepDeviceType::Primary);
	keeper->SetStreamType(KeepStreamType::Inaudible);

	// Parse file name for defaults.
	char fn_buffer[MAX_PATH];
	DWORD fn_size = GetModuleFileNameA(NULL, fn_buffer, MAX_PATH);
	if (fn_size != 0 && fn_size != MAX_PATH)
	{
		char* filename = strrchr(fn_buffer, '\\');
		if (filename)
		{
			filename++;
			DebugLog("Exe File Name: %s.", filename);
			ParseMode(keeper, filename);
		}
	}

	// Parse command line for arguments.
	if (const char* cmdln = GetCommandLineA())
	{
		// Skip program file name.
		while (*cmdln == ' ') { cmdln++; }
		if (cmdln[0] == '"')
		{
			cmdln++;
			while (*cmdln != '"' && *cmdln != 0) { cmdln++; }
			if (*cmdln == '"') { cmdln++; }
		}
		else
		{
			while (*cmdln != ' ' && *cmdln != 0) { cmdln++; }
		}
		while (*cmdln == ' ') { cmdln++; }

		if (*cmdln != 0)
		{
			DebugLog("Command Line: %s.", cmdln);
			ParseMode(keeper, cmdln);
		}
	}

#ifdef _DEBUG

	switch (keeper->GetDeviceType())
	{
		case KeepDeviceType::Primary:   DebugLog("Device Type: Primary."); break;
		case KeepDeviceType::All:       DebugLog("Device Type: All."); break;
		case KeepDeviceType::Analog:    DebugLog("Device Type: Analog."); break;
		case KeepDeviceType::Digital:   DebugLog("Device Type: Digital."); break;
		default:                        DebugLogError("Unknown Device Type."); break;
	}

	switch (keeper->GetStreamType())
	{
		case KeepStreamType::Silence:   DebugLog("Stream Type: Silence."); break;
		case KeepStreamType::Inaudible: DebugLog("Stream Type: Inaudible."); break;
		case KeepStreamType::Sine:      DebugLog("Stream Type: Sine (Frequency: %.3fHz; Amplitude: %.3f%%).", keeper->GetFrequency(), keeper->GetAmplitude() * 100.0); break;
		default:                        DebugLogError("Unknown Stream Type."); break;
	}

#endif

	hr = keeper->Main();
	if (FAILED(hr))
	{
#ifndef _DEBUG
		MessageBoxA(0, "Cannot run main code.", "Sound Keeper", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);
#endif
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

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
	return Main();
}

#endif
