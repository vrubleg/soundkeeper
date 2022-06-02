Sound Keeper v1.2.2 [2022/05/15]
https://veg.by/projects/soundkeeper/

Prevents SPDIF/HDMI digital audio playback devices from sleeping. Uses WASAPI, requires Windows 7+.

The program doesn't have GUI. It starts to do its job right after the process is started.
To close the program, just mute the Sound Keeper in the Volume Mixer or kill the soundkeeper.exe process.
To autorun, copy soundkeeper.exe into the startup directory (to open it, press Win+R, enter "shell:startup").

Default behavior can be changed by adding options to the Sound Keeper executable file name.
Primary audio output is used by default. Add "All" to executable file name to keep on all audio outputs.
Inaudible stream is used by default. Add "Zero" to executable file name to use stream of digital zeroes.

Supported behavior modes:
- "Primary" keeps on primary audio output only. Used by default.
- "All" keeps on all enabled audio outputs.
- "Digital" keeps on all enabled digital audio outputs (like it was in Sound Keeper v1.0).
- "Analog" keeps on all enabled analog audio outputs.

Supported stream types:
- "Zero" plays stream of zeroes (like it was in Sound Keeper v1.0). It may be not enough for some hardware.
- "Fluctuate" plays inaudible stream with lowest bit flipping from sample to sample. Used by default since v1.1.
- "Sine" plays 1Hz sine wave at 1% volume. The frequency and amplitude can be changed. Useful for analog outputs.

Sine stream parameters:
There are two parameters: F (frequency) and A (amplitude). The value goes right after the parameter character.
Low frequencies (below 20Hz) and high frequencies (above 20000Hz) with low amplitude (up to 10%) are inaudible.

Examples:
- SoundKeeperZeroAll.exe generates zero amplitude stream on all enabled audio outputs.
- SoundKeeperAll.exe generates default inaudible stream on all enabled audio outputs.
- SoundKeeperSineF10A5.exe generates 10Hz sine wave with 5% amplitude on primary audio output. It is inaudible.
- SoundKeeperSineF1000A100.exe generates 1000Hz sine wave with 100% amplitude. It is audible! Use it for testing.

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

[2022/XX/XX] v1.X.X:
- Self kill command is added. Run "soundkeeper kill" to stop running Sound Keeper instance.
- "Analog" switch was added. It works as the opposite of "Digital".

[2022/05/15] v1.2.2:
- Work as a dummy when no suitable devices found.
- Sound Keeper shoudn't prevent PC from automatic going into sleep mode on Windows 10.

[2021/11/05] v1.2.1:
- Sound Keeper works on Windows 11.
- The workaround that allowed PC to sleep had to be disabled on Windows 11.

[2021/10/30] v1.2.0:
- Sound Keeper doesn't prevent PC from automatic going into sleep mode on Windows 7.
- New "Sine" stream type which can be useful for analog outputs or too smart digital outputs.
- When a user starts a new Sound Keeper instance, the previous one is stopped automatically.
- "Fluctuate" stream type considers sample format of the output (16/24/32-bit integer, and 32-bit float).
- Command line parameters are supported. Example: "soundkeeper sine -f 1000 -a 10".
- The workaround for the Audio Service memory leak is enabled on affected Windows versions only (8, 8.1, and 10).

[2020/07/18] v1.1.0:
- Default behavior can be changed by adding options to the Sound Keeper executable file name.
- Primary audio output is used by default.
- Inaudible stream is used by default.
- Workaround for a Windows 10 bug which causes a memory leak in the Audio Service when audio output is busy.

[2020/03/14] v1.0.4: Fixed a potential memory leak when another program uses audio output in exclusive mode.
[2019/07/14] v1.0.3: Exclusive mode doesn't prevent Sound Keeper from working.
[2017/12/23] v1.0.2: 64-bit version is added.
[2017/12/21] v1.0.1: Waking PC up after sleeping doesn't prevent Sound Keeper from working.
[2014/12/24] v1.0.0: Initial release.

(C) 2014-2022 Evgeny Vrublevsky <me@veg.by>
