set RELEASEDIR=\\catsserver.jetcats.jet.efda.org\CopyArea\areaDetector\ADPICam
REM set RELEASEDIR=C:\ADPICam
set SOURCEDIR=D:\EPICS\support\areaDetector\ADPICam

REM https://social.technet.microsoft.com/Forums/windows/en-US/22f8ae0b-41e0-48c8-905a-71fe985600be/batch-files-and-the-remove-directory-command-rmdir?forum=w7itprogeneral
if exist %RELEASEDIR% RD /Q/S %RELEASEDIR%

timeout 1

if not exist %RELEASEDIR%\PICamApp\Scripts mkdir %RELEASEDIR%\PICamApp\Scripts
copy %SOURCEDIR%\PICamApp\Scripts\*.bat %RELEASEDIR%\PICamApp\Scripts

if not exist %RELEASEDIR%\op\opi\autoconvert mkdir %RELEASEDIR%\PICamApp\op\opi\autoconvert

copy %SOURCEDIR%\PICamApp\op\opi\autoconvert\*.opi %RELEASEDIR%\PICamApp\op\opi\autoconvert

if not exist %RELEASEDIR%\db mkdir %RELEASEDIR%\db
copy %SOURCEDIR%\db\*.template %RELEASEDIR%\db\
if not exist %RELEASEDIR%\dbd mkdir %RELEASEDIR%\dbd
copy %SOURCEDIR%\dbd\PICamWithActonApp.dbd %RELEASEDIR%\dbd\
copy %SOURCEDIR%\dbd\PICamApp.dbd %RELEASEDIR%\dbd\

if not exist %RELEASEDIR%\iocBoot\iocPICamWithActon mkdir %RELEASEDIR%\iocBoot\iocPICamWithActon
if not exist %RELEASEDIR%\iocBoot\iocPICam mkdir %RELEASEDIR%\iocBoot\iocPICam


copy %SOURCEDIR%\iocs\PICamIOC\iocBoot\iocPICam\st.cmd %RELEASEDIR%\iocBoot\iocPICam
SET TOP=../..
echo SET TOP=%TOP%> %RELEASEDIR%\iocBoot\iocPICam\start_epics.bat
echo SET ADCORE=%TOP%>> %RELEASEDIR%\iocBoot\iocPICam\start_epics.bat
echo SET ADPICAM=%TOP%>> %RELEASEDIR%\iocBoot\iocPICam\start_epics.bat
echo SET CALC=%TOP%>> %RELEASEDIR%\iocBoot\iocPICam\start_epics.bat
echo SET AUTOSAVE=%TOP%>> %RELEASEDIR%\iocBoot\iocPICam\start_epics.bat
echo ..\..\bin\windows-x64-static\PICamApp st.cmd>> %RELEASEDIR%\iocBoot\iocPICam\start_epics.bat

copy %SOURCEDIR%\iocs\PICamWithActonIOC\iocBoot\iocPICamWithActon\st.cmd %RELEASEDIR%\iocBoot\iocPICamWithActon
copy %SOURCEDIR%\iocs\PICamWithActonIOC\iocBoot\iocPICamWithActon\start_epics.bat %RELEASEDIR%\iocBoot\iocPICamWithActon
copy \epics\support\areaDetector\ADCore\iocBoot\commonPlugins.cmd %RELEASEDIR%\iocBoot

if not exist %RELEASEDIR%\bin\windows-x64-static mkdir %RELEASEDIR%\bin\windows-x64-static
copy \epics\iocTops\PrincetonSpectro\ARC_InstrumentApp\Acton\ARC_Instrument_x64.dll %RELEASEDIR%\bin\windows-x64-static
copy %SOURCEDIR%\x64\Release_LIB\PICamApp.exe %RELEASEDIR%\bin\windows-x64-static
copy %SOURCEDIR%\x64\Release_LIB\PICamWithActonApp.exe %RELEASEDIR%\bin\windows-x64-static
copy %SOURCEDIR%\x64\Release_LIB\caRepeater.exe %RELEASEDIR%\bin\windows-x64-static
