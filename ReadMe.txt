Sound Keeper v1.3.1 [2023/XX/XX]
https://veg.by/projects/soundkeeper/

Prevents SPDIF/HDMI digital audio playback devices from sleeping. Uses WASAPI, requires Windows 7+.

The program doesn't have GUI. It starts to do its job right after the process is started.
To close the program, just mute the Sound Keeper in the Volume Mixer or kill the soundkeeper.exe process.
To autorun, copy soundkeeper.exe into the startup directory (to open it, press Win+R, enter "shell:startup").

Default behavior can be changed by adding settings to the Sound Keeper executable file name or by passing them
as command line arguments. Setting names are case insensitive.

Supported device type settings:
- "Primary" keeps on primary audio output only. Used by default.
- "All" keeps on all enabled audio outputs.
- "Digital" keeps on all enabled SPDIF and HDMI audio outputs (like it was in Sound Keeper v1.0).
- "Analog" keeps on all enabled audio outputs except SPDIF and HDMI.

Supported stream type settings:
- "OpenOnly" opens audio output, but doesn't play anything. Sometimes it helps.
- "Zero" plays stream of zeroes. It may be not enough for some hardware.
- "Fluctuate" plays stream of zeroes with the smallest non-zero samples once in a second. Used by default.
- "Sine" plays 1Hz sine wave at 1% volume. The frequency and amplitude can be changed. Useful for analog outputs.
- "White", "Brown", or "Pink" play named noise, with the same parameters as the sine (except frequency).

Sine and noise stream parameters:
- F is frequency. Default: 1Hz. Applicable for: Fluctuate, Sine.
- A is amplitude. Default: 1%. If you want to use inaudible noise, set it to 0.1%. Applicable for: Sine, Noise.
- L is length of sound (in seconds). Default: infinite.
- W is waiting time between sounds if L is set. Use to enable periodic sound.
- T is transition or fading time. Default: 0.1 second. Applicable for: Sine, Noise.

Examples:
- SoundKeeperZeroAll.exe generates zero amplitude stream on all enabled audio outputs.
- SoundKeeperAll.exe generates default inaudible stream on all enabled audio outputs.
- SoundKeeperSineF10A5.exe generates 10Hz sine wave with 5% amplitude on primary audio output. It is inaudible.
- SoundKeeperSineF1000A100.exe generates 1000Hz sine wave with 100% amplitude. It is audible! Use it for testing.
- "SoundKeeper.exe brown -a 0.1" (settings are command line arguments) generates brown noise with 0.1% amplitude.

Known issues

When a program streams any audio (even silence), the system don't go into sleep mode automatically. Sound Keeper
uses the NtPowerInformation(SystemPowerInformation, ...) function to retrieve time when system is going to sleep,
and disables itself right before this time. On Windows 7, it works perfectly. Windows 10 waits for 2 minutes more
after any sound was streamed, so the PC goes into sleep mode after 2 minutes when Sound Keeper disabled itself.
For some reason, Windows 11 always reports that the system is going to sleep in 0 seconds. The workaround had to
be disabled on this OS until a better solution is found.

You can try to use "powercfg /REQUESTSOVERRIDE" to allow Windows go into sleep mode even when audio is streamed.
Execute "powercfg /REQUESTS" as admin while Sound Keeper is running to get information about your audio driver
that prevents the PC from sleeping when audio is streamed. Example how it may look like:
SYSTEM: [DRIVER] High Definition Audio Device (HDAUDIO\FUNC_01&VEN_10EC&...)

Execute such commands as admin to add this device into the ignore list:
powercfg /REQUESTSOVERRIDE DRIVER "High Definition Audio Device" SYSTEM
powercfg /REQUESTSOVERRIDE DRIVER "HDAUDIO\FUNC_01&VEN_10EC&..." SYSTEM
powercfg /REQUESTSOVERRIDE DRIVER "High Definition Audio Device (HDAUDIO\FUNC_01&VEN_10EC&...)" SYSTEM

To verify the ignore list, execute "powercfg /REQUESTSOVERRIDE". To revert your changes, execute:
powercfg /REQUESTSOVERRIDE DRIVER "High Definition Audio Device"
powercfg /REQUESTSOVERRIDE DRIVER "HDAUDIO\FUNC_01&VEN_10EC&..."
powercfg /REQUESTSOVERRIDE DRIVER "High Definition Audio Device (HDAUDIO\FUNC_01&VEN_10EC&...)"

What's new

v1.3.1 [2023/XX/XX]:
- A potential deadlock when audio devices are added or removed has been fixed.
- "Fluctuate" treats non-PCM output formats (like Dolby Atmos) as 24-bit instead of 16-bit.
- Frequency parameter is limited by half of current sample rate to avoid generation of unexpected noise.
- More detailed logs in debug builds. Debug output is flushed immediately, so it can be redirected to a file.

v1.3.0 [2022/07/28]:
- "Fluctuate" is 1 fluctuation per second by default. Frequency can be changed using the F parameter.
- Periodic playing of a sine sound with optional fading.
- "White", "Brown", and "Pink" noise signal types.
- Self kill command is added. Run "soundkeeper kill" to stop running Sound Keeper instance.
- "Analog" switch was added. It works as the opposite of "Digital".
- Ignores remote desktop audio device (this feature can be disabled using the "Remote" switch).
- New "OpenOnly" mode that just opens audio output, but doesn't stream anything.
- New "NoSleep" switch which disables PC sleep detection (Windows 7-10).
- The program is not confused anymore when PC auto sleep is disabled on Windows 10.

v1.2.2 [2022/05/15]:
- Work as a dummy when no suitable devices found.
- Sound Keeper shouldn't prevent PC from automatic going into sleep mode on Windows 10.

v1.2.1 [2021/11/05]:
- Sound Keeper works on Windows 11.
- The workaround that allowed PC to sleep had to be disabled on Windows 11.

v1.2.0 [2021/10/30]:
- Sound Keeper doesn't prevent PC from automatic going into sleep mode on Windows 7.
- New "Sine" stream type which can be useful for analog outputs or too smart digital outputs.
- When a user starts a new Sound Keeper instance, the previous one is stopped automatically.
- "Fluctuate" stream type considers sample format of the output (16/24/32-bit integer, and 32-bit float).
- Command line arguments are supported. Example: "soundkeeper sine -f 1000 -a 10".
- The workaround for the Audio Service memory leak is enabled on affected Windows versions only (8, 8.1, and 10).

v1.1.0 [2020/07/18]:
- Default behavior can be changed by adding options to the Sound Keeper executable file name.
- Primary audio output is used by default.
- Inaudible stream is used by default.
- Workaround for a Windows 10 bug which causes a memory leak in the Audio Service when audio output is busy.

v1.0.4 [2020/03/14]: Fixed a potential memory leak when another program uses audio output in exclusive mode.
v1.0.3 [2019/07/14]: Exclusive mode doesn't prevent Sound Keeper from working.
v1.0.2 [2017/12/23]: 64-bit version is added.
v1.0.1 [2017/12/21]: Waking PC up after sleeping doesn't prevent Sound Keeper from working.
v1.0.0 [2014/12/24]: Initial release.

(C) 2014-2022 Evgeny Vrublevsky <me@veg.by>
