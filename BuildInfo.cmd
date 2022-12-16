@echo off

where csi 1>nul 2>&1 && goto :ready

for /f "usebackq delims=#" %%a in (`"%programfiles(x86)%\Microsoft Visual Studio\Installer\vswhere" -latest -property installationPath`) do set VSDEVCMD_PATH=%%a\Common7\Tools\VsDevCmd.bat
set VSCMD_SKIP_SENDTELEMETRY=1
call "%VSDEVCMD_PATH%" -no_logo -startdir=none

where csi 1>nul 2>&1 && goto :ready

echo Warning: Can't find csi to execute BuildInfo.csx. BuildInfo.hpp won't be updated.
exit /b 0

:ready

csi BuildInfo.csx -- %*
