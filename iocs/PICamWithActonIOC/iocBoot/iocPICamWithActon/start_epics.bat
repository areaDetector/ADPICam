REM start medm -x -macro "P=13PICAM1:, R=cam1:" PICam.adl
REM start medm -x -macro "P=13PICAM1:, R=cam1:" ADBase.adl
if "%EPICS_HOST_ARCH%"=="" set EPICS_HOST_ARCH=windows-x64-static
if "%TOP%"=="" set TOP=../..
if "%ADCORE%"=="" set ADCORE=%TOP%
if "%ADPICAM%"=="" set ADPICAM=%TOP%
..\..\bin\%EPICS_HOST_ARCH%\PICamWithActonApp.exe st.cmd > start_epics.log 2>&1

