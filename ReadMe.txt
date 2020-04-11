Digital Sound Keeper v1.1.0 [2020/04/11]
http://veg.by/en/projects/soundkeeper/

Prevents SPDIF/HDMI digital playback devices from falling asleep. Uses WASAPI, requires Windows 7+.
To enable autorun, press Win+R, enter "shell:startup", press Enter, and copy soundkeeper.exe into the opened directory.
To disable autorun, close the program and then remove soundkeeper.exe from the startup directory.
To close the program, just mute the Sound Keeper in the Volume Mixer or simply kill the soundkeeper.exe process.
Default behavior can be changed by adding options to the Sound Keeper executable file name.
Primary audio output is used by default. Add "All" or "Digital" to executable file name to enable Sound Keeper on all or digital outputs only.
Inaudible stream is used by default. Add "Zero" to executable file name to use stream of digital zeroes (like it was in Sound Keeper v1.0).

Changelog

[2020/04/11] v1.1.0:
- Default behavior can be changed by adding options to the Sound Keeper executable file name.
- Primary audio output is used by default.
- Inaudible stream is used by default.
- A workaround for a Windows 10 bug which causes a memory leak in the Audio Service when audio output is busy.

[2020/03/14] v1.0.4: Fixed a potential memory leak when another program uses audio output in exclusive mode.
[2019/07/14] v1.0.3: Exclusive mode doesn't prevent Sound Keeper from working.
[2017/12/23] v1.0.2: 64-bit version is added.
[2017/12/21] v1.0.1: Waking PC up after sleeping doesn't prevent Sound Keeper from working.
[2014/12/24] v1.0.0: Initial release.

(C) 2014-2020 Evgeny Vrublevsky <me@veg.by>
