set SUPPORT=C:\jhSrc\synApps_5_7\support
set AREA_DETECTOR=%SUPPORT%\areaDetector_2_git
set ASYN=%SUPPORT%\asyn-4-23
set EPICS_DISPLAY_PATH=%AREA_DETECTOR%\ADPICam\PICamApp\op\adl;%AREA_DETECTOR%\ADCore\ADApp\op\adl;%ASYN%\opi\medm
echo %EPICS_DISPLAY_PATH%
start medm -x -macro "P=13PICAM1:, R=cam1:" PICAMBase.adl
