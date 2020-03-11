Digital Sound Keeper v1.0.4 [2020/03/06]
http://veg.by/en/projects/soundkeeper/

Prevents SPDIF/HDMI digital playback devices from falling asleep. Uses WASAPI, requires Windows 7+.
To enable autorun, press Win+R, enter "shell:startup", press Enter, and copy soundkeeper.exe into the opened directory.
To disable autorun, close the program and then remove soundkeeper.exe from the startup directory.
To close the program, just mute the Sound Keeper in the Volume Mixer or simply kill the soundkeeper.exe process.

Features:
- Sound Keeper is fully automatic and doesn't require any user interaction.
- Sound Keeper can keep sound on many digital sound outputs simultaneously (e.g. SPDIF and HDMI).
- Sound Keeper detects new digital sound outputs on the fly (e.g. when you connected TV via HDMI).

Comparison:         Sound Keeper v1.0   SPDIF Keep Alive v1.2
Fully automatic:    Yes                 No
Multiple outputs:   Yes                 No
GUI:                No                  Yes
Requires .NET:      No                  Yes
Executable size:    17KB                668KB
CPU usage:          0.004%              0.06%               (on Intel Core i5 4460)
Memory:             1636KB              13704KB             (Private Working Set)

Changelog:
[2014/12/24] v1.0.0: Initial release.
[2017/12/21] v1.0.1: Waking PC up after sleeping doesn't prevent Sound Keeper from working.
[2017/12/23] v1.0.2: 64-bit version was added.
[2019/07/14] v1.0.3: Exclusive mode doesn't prevent Sound Keeper from working.
[2020/03/06] v1.0.4: A memory leak is fixed.

(C) 2014-2020 Evgeny Vrublevsky <me@veg.by>
