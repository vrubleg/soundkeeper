@echo off
setlocal

set NO_ERRORS=0

for %%a in (%*) do (
	if "%%a"=="--no-errors" set NO_ERRORS=1
)

where csi 1>nul 2>&1 && goto :ready

set VSWHERE_PATH=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE_PATH%" set VSWHERE_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE_PATH%" goto :error
for /f "usebackq delims=#" %%a in (`"%VSWHERE_PATH%" -latest -property installationPath`) do set VSDEVCMD_PATH=%%a\Common7\Tools\VsDevCmd.bat
if not exist "%VSDEVCMD_PATH%" goto :error
set VSCMD_SKIP_SENDTELEMETRY=1
call "%VSDEVCMD_PATH%" -no_logo -startdir=none

where csi 1>nul 2>&1 && goto :ready

:error

if "%NO_ERRORS%"=="1" (
	echo Warning: Can't find csi to execute BuildInfo.csx. BuildInfo.hpp won't be updated.
	exit /b 0
) else (
	echo Error: Can't find csi to execute BuildInfo.csx. BuildInfo.hpp won't be updated.
	exit /b 1
)

:ready

csi BuildInfo.csx -- BuildInfo.hpp %*
