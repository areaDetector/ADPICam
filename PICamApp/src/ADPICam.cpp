/**
 Copyright (c) 2015, UChicago Argonne, LLC
 See LICENSE file.
*/
/* PICam.cpp */
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <stdexcept>

#include <epicsTime.h>
#include <epicsExit.h>
#include "ADDriver.h"
#include <epicsExport.h>

#include "ADPICam.h"


#define MAX_ENUM_STATES 16

/* define C99 standard __func__ to come from MS's __FUNCTION__ */
#if defined ( _MSC_VER )
#define __func__ __FUNCTION__
#endif

static void piHandleNewImageTaskC(void *drvPvt);

extern "C" {
/** Configuration command for PICAM driver; creates a new PICam object.
 * \param[in] portName The name of the asyn port driver to be created.
 * \param[in] maxBuffers The maximum number of NDArray buffers that the
 *            NDArrayPool for this driver is
 *            allowed to allocate. Set this to -1 to allow an unlimited number
 *            of buffers.
 * \param[in] maxMemory The maximum amount of memory that the NDArrayPool for
 *            this driver is allowed to allocate. Set this to -1 to allow an
 *            unlimited amount of memory.
 * \param[in] priority The thread priority for the asyn port driver thread if
 *            ASYN_CANBLOCK is set in asynFlags.
 * \param[in] stackSize The stack size for the asyn port driver thread if
 *            ASYN_CANBLOCK is set in asynFlags.
 */
	int PICamConfig(const char *portName, int maxBuffers,
			size_t maxMemory, int priority, int stackSize) {
		new ADPICam(portName, maxBuffers, maxMemory, priority, stackSize);
		return (asynSuccess);
	}

	/** Configuration command for PICAM driver; creates a new PICam object.
	 * \param[in]  demoCameraName String identifying demoCameraName
	 */
	int PICamAddDemoCamera(const char *demoCameraName) {
		int status = asynSuccess;

		status = ADPICam::piAddDemoCamera(demoCameraName);
		return (status);
	}

	/**
	 * Callback function for exit hook
	 */
	static void exitCallbackC(void *pPvt){
		ADPICam *pADPICam = (ADPICam*)pPvt;
		delete pADPICam;
	}
}
ADPICam * ADPICam::ADPICam_Instance = NULL;
const char *ADPICam::notAvailable = "N/A";
const char *ADPICam::driverName = "PICam";


/**
 * Constructor
 * \param[in] portName The name of the asyn port driver to be created.
 * \param[in] maxBuffers The maximum number of NDArray buffers that the
 *            NDArrayPool for this driver is
 *            allowed to allocate. Set this to -1 to allow an unlimited number
 *            of buffers.
 * \param[in] maxMemory The maximum amount of memory that the NDArrayPool for
 *            this driver is allowed to allocate. Set this to -1 to allow an
 *            unlimited amount of memory.
 * \param[in] priority The thread priority for the asyn port driver thread if
 *            ASYN_CANBLOCK is set in asynFlags.
 * \param[in] stackSize The stack size for the asyn port driver thread if
 *            ASYN_CANBLOCK is set in asynFlags.
 *
 */
ADPICam::ADPICam(const char *portName, int maxBuffers, size_t maxMemory,
        int priority, int stackSize) :
        ADDriver(portName, 1, int(NUM_PICAM_PARAMS), maxBuffers, maxMemory,
        asynEnumMask, asynEnumMask, ASYN_CANBLOCK, 1, priority, stackSize) {
    int status = asynSuccess;
    static const char *functionName = "PICam";
    const char *emptyStr = "";
    pibln libInitialized;
    PicamCameraID demoId;
    PicamError error = PicamError_None;
    const pichar *errorString;
    int enableDisplay = 1;

    currentCameraHandle = NULL;
    selectedCameraIndex = -1;
    availableCamerasCount = 0;
    unavailableCamerasCount = 0;
    imageThreadKeepAlive = true;

    error = Picam_IsLibraryInitialized(&libInitialized);
    if (libInitialized) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s%s Found Picam Library initialized, unitializing\n", driverName,
            functionName);
        error = Picam_UninitializeLibrary();
        if (error != PicamError_None) {
            Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                    &errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s%s Trouble Uninitializing Picam Library: %s\n", driverName,
                    functionName, errorString);
            Picam_DestroyString(errorString);
            return;
        }
    }

    error = Picam_InitializeLibrary();
    if (error != PicamError_None) {
        //Try Again.
        error = Picam_InitializeLibrary();
        if (error != PicamError_None) {
            Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                    &errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "------------------------------------------------\n"
                    "%s%s Trouble Initializing Picam Library: %s\n"
                    "------------------------------------------------\n",
					driverName,
                    functionName,
					errorString);
            Picam_DestroyString(errorString);
            return;
        }
    }
    error = Picam_IsLibraryInitialized(&libInitialized);
    if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                &errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "---------------------------------------------------------\n"
                "%s%s Trouble Checking if Picam Library is initialized: %s\n"
                "---------------------------------------------------------\n",
                driverName,
                functionName, errorString);
        Picam_DestroyString(errorString);
        return;
    }
    if (!libInitialized) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "------------------------------------------------\n"
        		"%s%s Trouble Initializing Picam Library\n"
                "------------------------------------------------\n",
				driverName,
                functionName);
        return;    // This was that last chance.  Can't do anything.
    }

    ADPICam_Instance = this;

    /*
    Connect a SOPHIA 2048BX virtual camera to load -- if no physical camera
    is plugged in when Available Cameras are generated, the virtual camera
    gets used.
    */
        PicamModel demoToConnect = PicamModel_Sophia2048BExcelon;
        error = Picam_ConnectDemoCamera(demoToConnect,
                "CamNotFoundOnInit", &demoId);
        if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error,
                        error,
                                        &errorString);
        const char *demoModelName;
        Picam_GetEnumerationString(PicamEnumeratedType_Model,
                        demoToConnect,
                                        &demoModelName);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                        "-------------------------------------------------\n"
                        "Cannot connect to demo camera %s."
                        "Cannot run without a detector.\n"
                        "Error=%s \n"
                        "-------------------------------------------------\n",
                                        demoModelName,
                                        errorString);
        Picam_DestroyString(demoModelName);
        Picam_DestroyString(errorString);
        return;
        }
        error = Picam_OpenCamera(&demoId, &currentCameraHandle);
        if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error,
                        error,
                                        &errorString);
        const char *demoModelName;
        Picam_GetEnumerationString(PicamEnumeratedType_Model,
                        demoToConnect,
                                        &demoModelName);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "------------------------------------------------\n"
                        "Trouble opening demo camera %s \n%s"
                        "------------------------------------------------\n",
                                        demoModelName, errorString);
        Picam_DestroyString(demoModelName);
        Picam_DestroyString(errorString);
        return;
        }
    PicamAdvanced_GetCameraDevice(currentCameraHandle, &currentDeviceHandle);
    selectedCameraIndex = 0;
    createParam(PICAM_VersionNumberString, asynParamOctet,
            &PICAM_VersionNumber);
    // Available Camera List
    createParam(PICAM_AvailableCamerasString, asynParamInt32,
            &PICAM_AvailableCameras);
    createParam(PICAM_CameraInterfaceString, asynParamOctet,
            &PICAM_CameraInterface);
    createParam(PICAM_SensorNameString, asynParamOctet, &PICAM_SensorName);
    createParam(PICAM_SerialNumberString, asynParamOctet, &PICAM_SerialNumber);
    createParam(PICAM_FirmwareRevisionString, asynParamOctet,
            &PICAM_FirmwareRevision);
    //Unavailable Camera List
    createParam(PICAM_UnavailableCamerasString, asynParamInt32,
            &PICAM_UnavailableCameras);
    createParam(PICAM_CameraInterfaceUnavailableString, asynParamOctet,
            &PICAM_CameraInterfaceUnavailable);
    createParam(PICAM_SensorNameUnavailableString, asynParamOctet,
            &PICAM_SensorNameUnavailable);
    createParam(PICAM_SerialNumberUnavailableString, asynParamOctet,
            &PICAM_SerialNumberUnavailable);
    createParam(PICAM_FirmwareRevisionUnavailableString, asynParamOctet,
            &PICAM_FirmwareRevisionUnavailable);
    //Shutter
    piCreateAndIndexADParam(PICAM_ExposureTimeString,
            ADAcquireTime,
            PICAM_ExposureTimeExists,
            PICAM_ExposureTimeRelevant,
            PicamParameter_ExposureTime);
    piCreateAndIndexADParam(PICAM_ShutterClosingDelayString,
            ADShutterCloseDelay,
            PICAM_ShutterClosingDelayExists,
            PICAM_ShutterClosingDelayRelevant,
            PicamParameter_ShutterClosingDelay);
    piCreateAndIndexPIParam(PICAM_ShutterDelayResolutionString,
            asynParamInt32,
            PICAM_ShutterDelayResolution,
            PICAM_ShutterDelayResolutionExists,
            PICAM_ShutterDelayResolutionRelevant,
            PicamParameter_ShutterDelayResolution);
    piCreateAndIndexADParam(PICAM_ShutterOpeningDelayString,
            ADShutterOpenDelay,
            PICAM_ShutterOpeningDelayExists,
            PICAM_ShutterOpeningDelayRelevant,
            PicamParameter_ShutterOpeningDelay);
    piCreateAndIndexPIParam(PICAM_ShutterTimingModeString, asynParamInt32,
            PICAM_ShutterTimingMode,
            PICAM_ShutterTimingModeExists,
            PICAM_ShutterTimingModeRelevant,
            PicamParameter_ShutterTimingMode);

    //Intensifier
    piCreateAndIndexPIParam(PICAM_BracketGatingString,
            asynParamInt32,
            PICAM_BracketGating,
            PICAM_BracketGatingExists,
            PICAM_BracketGatingRelevant,
            PicamParameter_BracketGating);
    //TODO  CustomModulationSequence needs Modulation Type
    piCreateAndIndexPIModulationsParam(PICAM_CustomModulationSequenceString,
            PICAM_CustomModulationSequenceExists,
            PICAM_CustomModulationSequenceRelevant,
            PicamParameter_CustomModulationSequence);
    //TODO  DifEndingGate  needs pulse type
    piCreateAndIndexPIPulseParam(PICAM_DifEndingGateString,
            PICAM_DifEndingGateExists,
            PICAM_DifEndingGateRelevant,
            PicamParameter_DifEndingGate);
    //TODO  DifStartingGate needs pulse type
    piCreateAndIndexPIPulseParam(PICAM_DifStartingGateString,
            PICAM_DifStartingGateExists,
            PICAM_DifStartingGateRelevant,
            PicamParameter_DifStartingGate);
    piCreateAndIndexPIParam(PICAM_EMIccdGainString, asynParamInt32,
            PICAM_EMIccdGain, PICAM_EMIccdGainExists,
            PICAM_EMIccdGainRelevant, PicamParameter_EMIccdGain);
    piCreateAndIndexPIParam(PICAM_EMIccdGainControlModeString, asynParamInt32,
            PICAM_EMIccdGainControlMode, PICAM_EMIccdGainControlModeExists,
            PICAM_EMIccdGainControlModeRelevant,
            PicamParameter_EMIccdGainControlMode);
    piCreateAndIndexPIParam(PICAM_EnableIntensifierString, asynParamInt32,
            PICAM_EnableIntensifier,
            PICAM_EnableIntensifierExists,
            PICAM_EnableIntensifierRelevant,
            PicamParameter_EnableIntensifier);
    piCreateAndIndexPIParam(PICAM_EnableModulationString, asynParamInt32,
            PICAM_EnableModulation,
            PICAM_EnableModulationExists,
            PICAM_EnableModulationRelevant, PicamParameter_EnableModulation);
    piCreateAndIndexPIParam(PICAM_GatingModeString, asynParamInt32,
            PICAM_GatingMode,
            PICAM_GatingModeExists,
            PICAM_GatingModeRelevant,
            PicamParameter_GatingMode);
    piCreateAndIndexPIParam(PICAM_GatingSpeedString, asynParamOctet,
            PICAM_GatingSpeed,
            PICAM_GatingSpeedExists,
            PICAM_GatingSpeedRelevant,
            PicamParameter_GatingSpeed);
    piCreateAndIndexPIParam(PICAM_IntensifierDiameterString, asynParamFloat64,
            PICAM_IntensifierDiameter,
            PICAM_IntensifierDiameterExists,
            PICAM_IntensifierDiameterRelevant,
            PicamParameter_IntensifierDiameter);
    piCreateAndIndexPIParam(PICAM_IntensifierGainString, asynParamInt32,
            PICAM_IntensifierGain, PICAM_IntensifierGainExists,
            PICAM_IntensifierGainRelevant, PicamParameter_IntensifierGain);
    piCreateAndIndexPIParam(PICAM_IntensifierOptionsString, asynParamOctet,
            PICAM_IntensifierOptions, PICAM_IntensifierOptionsExists,
            PICAM_IntensifierOptionsRelevant,
            PicamParameter_IntensifierOptions);
    piCreateAndIndexPIParam(PICAM_IntensifierStatusString, asynParamOctet,
            PICAM_IntensifierStatus, PICAM_IntensifierStatusExists,
            PICAM_IntensifierStatusRelevant, PicamParameter_IntensifierStatus);
    piCreateAndIndexPIParam(PICAM_ModulationDurationString, asynParamFloat64,
            PICAM_ModulationDuration, PICAM_ModulationDurationExists,
            PICAM_ModulationDurationRelevant,
            PicamParameter_ModulationDuration);
    piCreateAndIndexPIParam(PICAM_ModulationFrequencyString, asynParamFloat64,
            PICAM_ModulationFrequency, PICAM_ModulationFrequencyExists,
            PICAM_ModulationFrequencyRelevant,
            PicamParameter_ModulationFrequency);
    piCreateAndIndexPIParam(PICAM_PhosphorDecayDelayString, asynParamFloat64,
            PICAM_PhosphorDecayDelay, PICAM_PhosphorDecayDelayExists,
            PICAM_PhosphorDecayDelayRelevant,
            PicamParameter_PhosphorDecayDelay);
    piCreateAndIndexPIParam(PICAM_PhosphorDecayDelayResolutionString,
            asynParamInt32,
            PICAM_PhosphorDecayDelayResolution,
            PICAM_PhosphorDecayDelayResolutionExists,
            PICAM_PhosphorDecayDelayResolutionRelevant,
            PicamParameter_PhosphorDecayDelayResolution);
    piCreateAndIndexPIParam(PICAM_PhosphorTypeString, asynParamOctet,
            PICAM_PhosphorType, PICAM_PhosphorTypeExists,
            PICAM_PhosphorTypeRelevant, PicamParameter_PhosphorType);
    piCreateAndIndexPIParam(PICAM_PhotocathodeSensitivityString, asynParamOctet,
            PICAM_PhotocathodeSensitivity,
            PICAM_PhotocathodeSensitivityExists,
            PICAM_PhotocathodeSensitivityRelevant,
            PicamParameter_PhotocathodeSensitivity);
    //TODO Repetitive Gate needs Pulse Type
    piCreateAndIndexPIPulseParam(PICAM_RepetitiveGateString,
            PICAM_RepetitiveGateExists,
            PICAM_RepetitiveGateRelevant,
            PicamParameter_RepetitiveGate);
    piCreateAndIndexPIParam(PICAM_RepetitiveModulationString, asynParamFloat64,
            PICAM_RepetitiveModulation, PICAM_RepetitiveModulationPhaseExists,
            PICAM_RepetitiveModulationPhaseRelevant,
            PicamParameter_RepetitiveModulationPhase);
    piCreateAndIndexPIParam(PICAM_SequentialStartingModulationPhaseString,
            asynParamFloat64, PICAM_SequentialStartingModulationPhase,
            PICAM_SequentialStartingModulationPhaseExists,
            PICAM_SequentialStartingModulationPhaseRelevant,
            PicamParameter_SequentialStartingModulationPhase);
    piCreateAndIndexPIParam(PICAM_SequentialEndingModulationPhaseString,
            asynParamFloat64, PICAM_SequentialEndingModulationPhase,
            PICAM_SequentialEndingModulationPhaseExists,
            PICAM_SequentialEndingModulationPhaseRelevant,
            PicamParameter_SequentialEndingModulationPhase);
    //TODO SequentialEndingGate needs Pulse Type
    piCreateAndIndexPIPulseParam(PICAM_SequentialEndingGateString,
            PICAM_SequentialEndingGateExists,
            PICAM_SequentialEndingGateRelevant,
            PicamParameter_SequentialEndingGate);
    piCreateAndIndexPIParam(PICAM_SequentialGateStepCountString, asynParamInt32,
            PICAM_SequentialGateStepCount,
            PICAM_SequentialGateStepCountExists,
            PICAM_SequentialGateStepCountRelevant,
            PicamParameter_SequentialGateStepCount);
    piCreateAndIndexPIParam(PICAM_SequentialGateStepIterationsString,
            asynParamInt32, PICAM_SequentialGateStepIterations,
            PICAM_SequentialGateStepIterationsExists,
            PICAM_SequentialGateStepIterationsRelevant,
            PicamParameter_SequentialGateStepIterations);
    //TODO SequentialStartingGate needs Pulse Type
    piCreateAndIndexPIPulseParam(PICAM_SequentialStartingGateString,
            PICAM_SequentialStartingGateExists,
            PICAM_SequentialStartingGateRelevant,
            PicamParameter_SequentialStartingGate);

    //Analog to Digital Conversion
    piCreateAndIndexPIParam(PICAM_AdcAnalogGainString, asynParamInt32,
            PICAM_AdcAnalogGain, PICAM_AdcAnalogGainExists,
            PICAM_AdcAnalogGainRelevant, PicamParameter_AdcAnalogGain);
    piCreateAndIndexPIParam(PICAM_AdcBitDepthString, asynParamInt32,
            PICAM_AdcBitDepth, PICAM_AdcBitDepthExists,
            PICAM_AdcBitDepthRelevant, PicamParameter_AdcBitDepth);
    piCreateAndIndexPIParam(PICAM_AdcEMGainString, asynParamInt32,
            PICAM_AdcEMGain, PICAM_AdcEMGainExists, PICAM_AdcEMGainRelevant,
            PicamParameter_AdcEMGain);
    piCreateAndIndexPIParam(PICAM_AdcQualityString, asynParamInt32,
            PICAM_AdcQuality, PICAM_AdcQualityExists,
            PICAM_AdcQualityRelevant, PicamParameter_AdcQuality);
    piCreateAndIndexPIParam(PICAM_AdcSpeedString, asynParamInt32, PICAM_AdcSpeed,
            PICAM_AdcSpeedExists, PICAM_AdcSpeedRelevant,
            PicamParameter_AdcSpeed);
    piCreateAndIndexPIParam(PICAM_CorrectPixelBiasString, asynParamInt32,
            PICAM_CorrectPixelBias, PICAM_CorrectPixelBiasExists,
            PICAM_CorrectPixelBiasRelevant, PicamParameter_CorrectPixelBias);

    // Hardware I/O
    piCreateAndIndexPIPulseParam(PICAM_AuxOutputString,
            PICAM_AuxOutputExists,
            PICAM_AuxOutputRelevant,
            PicamParameter_AuxOutput);
    piCreateAndIndexPIParam(PICAM_EnableModulationOutputSignalString,
            asynParamInt32, PICAM_EnableModulationOutputSignal,
            PICAM_EnableModulationOutputSignalExists,
            PICAM_EnableModulationOutputSignalRelevant,
            PicamParameter_EnableModulationOutputSignal);
    piCreateAndIndexPIParam(PICAM_ModulationOutputSignalFrequencyString,
            asynParamFloat64, PICAM_ModulationOutputSignalFrequency,
            PICAM_EnableModulationOutputSignalFrequencyExists,
            PICAM_EnableModulationOutputSignalAmplitudeRelevant,
            PicamParameter_ModulationOutputSignalFrequency);
    piCreateAndIndexPIParam(PICAM_ModulationOutputSignalAmplitudeString,
            asynParamFloat64, PICAM_ModulationOutputSignalAmplitude,
            PICAM_EnableModulationOutputSignalAmplitudeExists,
            PICAM_EnableModulationOutputSignalAmplitudeRelevant,
            PicamParameter_ModulationOutputSignalAmplitude);
    piCreateAndIndexPIParam(PICAM_EnableSyncMasterString, asynParamInt32,
            PICAM_EnableSyncMaster, PICAM_EnableSyncMasterExists,
            PICAM_EnableSyncMasterRelevant, PicamParameter_EnableSyncMaster);
    piCreateAndIndexPIParam(PICAM_InvertOutputSignalString, asynParamInt32,
            PICAM_InvertOutputSignal, PICAM_InvertOutputSignalExists,
            PICAM_InvertOutputSignalRelevant,
            PicamParameter_InvertOutputSignal);
    piCreateAndIndexPIParam(PICAM_OutputSignalString, asynParamInt32,
            PICAM_OutputSignal, PICAM_OutputSignalExists,
            PICAM_OutputSignalRelevant, PicamParameter_OutputSignal);
    piCreateAndIndexPIParam(PICAM_SyncMaster2DelayString, asynParamFloat64,
            PICAM_SyncMaster2Delay, PICAM_SyncMaster2DelayExists,
            PICAM_SyncMaster2DelayRelevant, PicamParameter_SyncMaster2Delay);
    piCreateAndIndexPIParam(PICAM_TriggerCouplingString, asynParamInt32,
            PICAM_TriggerCoupling, PICAM_TriggerCouplingExists,
            PICAM_TriggerCouplingRelevant, PicamParameter_TriggerCoupling);
    piCreateAndIndexPIParam(PICAM_TriggerDeterminationString, asynParamInt32,
            PICAM_TriggerDetermination, PICAM_TriggerDeterminationExists,
            PICAM_TriggerDeterminationRelevant,
            PicamParameter_TriggerDetermination);
    piCreateAndIndexPIParam(PICAM_TriggerFrequencyString, asynParamFloat64,
            PICAM_TriggerFrequency, PICAM_TriggerFrequencyExists,
            PICAM_TriggerFrequencyRelevant, PicamParameter_TriggerFrequency);
    piCreateAndIndexADParam(PICAM_TriggerResponseString,
            ADTriggerMode,
            PICAM_TriggerResponseExists,
            PICAM_TriggerResponseRelevant,
            PicamParameter_TriggerResponse);
    piCreateAndIndexPIParam(PICAM_TriggerSourceString, asynParamInt32,
            PICAM_TriggerSource, PICAM_TriggerSourceExists,
            PICAM_TriggerSourceRelevant, PicamParameter_TriggerSource);
    piCreateAndIndexPIParam(PICAM_TriggerTerminationString, asynParamInt32,
            PICAM_TriggerTermination, PICAM_TriggerTerminationExists,
            PICAM_TriggerTerminationRelevant,
            PicamParameter_TriggerTermination);
    piCreateAndIndexPIParam(PICAM_TriggerThresholdString, asynParamFloat64,
            PICAM_TriggerThreshold, PICAM_TriggerThresholdExists,
            PICAM_TriggerThresholdRelevant, PicamParameter_TriggerThreshold);

    // Readout Control
    piCreateAndIndexPIParam(PICAM_AccumulationsString, asynParamInt32,
            PICAM_Accumulations, PICAM_AccumulationsExists,
            PICAM_AccumulationsRelevant, PicamParameter_Accumulations);
    piCreateAndIndexPIParam(PICAM_EnableNondestructiveReadoutString,
            asynParamInt32, PICAM_EnableNondestructiveReadout,
            PICAM_EnableNondestructiveReadoutExists,
            PICAM_EnableNondestructiveReadoutRelevant,
            PicamParameter_EnableNondestructiveReadout);
    piCreateAndIndexPIParam(PICAM_KineticsWindowHeightString, asynParamInt32,
            PICAM_KineticsWindowHeight, PICAM_KineticsWindowHeightExists,
            PICAM_KineticsWindowHeightRelevant,
            PicamParameter_KineticsWindowHeight);
    piCreateAndIndexPIParam(PICAM_NondestructiveReadoutPeriodString,
            asynParamFloat64, PICAM_NondestructiveReadoutPeriod,
            PICAM_NondestructiveReadoutPeriodExists,
            PICAM_NondestructiveReadoutPeriodRelevant,
            PicamParameter_NondestructiveReadoutPeriod);
    piCreateAndIndexPIParam(PICAM_ReadoutControlModeString, asynParamInt32,
            PICAM_ReadoutControlMode, PICAM_ReadoutControlModeExists,
            PICAM_ReadoutControlModeRelevant,
            PicamParameter_ReadoutControlMode);
    piCreateAndIndexPIParam(PICAM_ReadoutOrientationString, asynParamOctet,
            PICAM_ReadoutOrientation, PICAM_ReadoutOrientationExists,
            PICAM_ReadoutOrientationRelevant,
            PicamParameter_ReadoutOrientation);
    piCreateAndIndexPIParam(PICAM_ReadoutPortCountString, asynParamInt32,
            PICAM_ReadoutPortCount, PICAM_ReadoutPortCountExists,
            PICAM_ReadoutPortCountRelevant, PicamParameter_ReadoutPortCount);
    piCreateAndIndexPIParam(PICAM_ReadoutTimeCalcString, asynParamFloat64,
            PICAM_ReadoutTimeCalc, PICAM_ReadoutTimeCalculationExists,
            PICAM_ReadoutTimeCalculationRelevant,
            PicamParameter_ReadoutTimeCalculation);
    piCreateAndIndexPIParam(PICAM_VerticalShiftRateString, asynParamInt32,
            PICAM_VerticalShiftRate, PICAM_VerticalShiftRateExists,
            PICAM_VerticalShiftRateRelevant, PicamParameter_VerticalShiftRate);

    // Data Acquisition
    piCreateAndIndexPIParam(PICAM_DisableDataFormattingString, asynParamInt32,
            PICAM_DisableDataFormatting, PICAM_DisableDataFormattingExists,
            PICAM_DisableDataFormattingRelevant,
            PicamParameter_DisableDataFormatting);
    piCreateAndIndexPIParam(PICAM_ExactReadoutCountMaxString, asynParamInt32,
            PICAM_ExactReadoutCountMax, PICAM_ExactReadoutCountMaximumExists,
            PICAM_ExactReadoutCountMaximumRelevant,
            PicamParameter_ExactReadoutCountMaximum);
    piCreateAndIndexPIParam(PICAM_FrameRateCalcString, asynParamFloat64,
            PICAM_FrameRateCalc, PICAM_FrameRateCalculationExists,
            PICAM_FrameRateCalculationRelevant,
            PicamParameter_FrameRateCalculation);
    piCreateAndIndexPIParam(PICAM_FramesPerReadoutString, asynParamInt32,
            PICAM_FramesPerReadout, PICAM_FramesPerReadoutExists,
            PICAM_FramesPerReadoutRelevant, PicamParameter_FramesPerReadout);
    piCreateAndIndexADParam(PICAM_FrameSizeString, NDArraySize,
            PICAM_FrameSizeExists,
            PICAM_FrameSizeRelevant,
            PicamParameter_FrameSize);
    piCreateAndIndexPIParam(PICAM_FrameStrideString, asynParamInt32,
            PICAM_FrameStride, PICAM_FrameStrideExists,
            PICAM_FrameStrideRelevant, PicamParameter_FrameStride);
    piCreateAndIndexPIParam(PICAM_FrameTrackingBitDepthString, asynParamInt32,
            PICAM_FrameTrackingBitDepth, PICAM_FrameTrackingBitDepthExists,
            PICAM_FrameTrackingBitDepthRelevant,
            PicamParameter_FrameTrackingBitDepth);
    piCreateAndIndexPIParam(PICAM_GateTrackingString, asynParamInt32,
            PICAM_GateTracking, PICAM_GateTrackingExists,
            PICAM_GateTrackingRelevant,
            PicamParameter_GateTracking);
    piCreateAndIndexPIParam(PICAM_GateTrackingBitDepthString, asynParamInt32,
            PICAM_GateTrackingBitDepth,
            PICAM_GateTrackingBitDepthExists,
            PICAM_GateTrackingBitDepthRelevant,
            PicamParameter_GateTrackingBitDepth);
    piCreateAndIndexPIParam(PICAM_ModulationTrackingString, asynParamInt32,
            PICAM_ModulationTracking,
            PICAM_ModulationTrackingExists,
            PICAM_ModulationTrackingRelevant,
            PicamParameter_ModulationTracking);
    piCreateAndIndexPIParam(PICAM_ModulationTrackingBitDepthString,
            asynParamInt32,
            PICAM_ModulationTrackingBitDepth,
            PICAM_ModulationTrackingBitDepthExists,
            PICAM_ModulationTrackingBitDepthRelevant,
            PicamParameter_ModulationTrackingBitDepth);
    piCreateAndIndexPIParam(PICAM_NormalizeOrientationString, asynParamInt32,
            PICAM_NormalizeOrientation,
            PICAM_NormalizeOrientationExists,
            PICAM_NormalizeOrientationRelevant,
            PicamParameter_NormalizeOrientation);
    piCreateAndIndexPIParam(PICAM_OnlineReadoutRateCalcString, asynParamFloat64,
            PICAM_OnlineReadoutRateCalc,
            PICAM_OnlineReadoutRateCalculationExists,
            PICAM_OnlineReadoutRateCalculationRelevant,
            PicamParameter_OnlineReadoutRateCalculation);
    piCreateAndIndexPIParam(PICAM_OrientationString, asynParamOctet,
            PICAM_Orientation,
            PICAM_OrientationExists,
            PICAM_OrientationRelevant,
            PicamParameter_Orientation);
    piCreateAndIndexPIParam(PICAM_PhotonDetectionModeString, asynParamInt32,
            PICAM_PhotonDetectionMode,
            PICAM_PhotonDetectionModeExists,
            PICAM_PhotonDetectionModeRelevant,
            PicamParameter_PhotonDetectionMode);
    piCreateAndIndexPIParam(PICAM_PhotonDetectionThresholdString,
            asynParamFloat64, PICAM_PhotonDetectionThreshold,
            PICAM_PhotonDetectionThresholdExists,
            PICAM_PhotonDetectionThresholdRelevant,
            PicamParameter_PhotonDetectionThreshold);
    piCreateAndIndexPIParam(PICAM_PixelBitDepthString, asynParamInt32,
            PICAM_PixelBitDepth,
            PICAM_PixelBitDepthExists,
            PICAM_PixelBitDepthRelevant,
            PicamParameter_PixelBitDepth);
    piCreateAndIndexPIParam(PICAM_PixelFormatString, asynParamInt32,
            PICAM_PixelFormat,
            PICAM_PixelFormatExists,
            PICAM_PixelFormatRelevant,
            PicamParameter_PixelFormat);
    piCreateAndIndexPIParam(PICAM_ReadoutCountString, asynParamInt32,
            PICAM_ReadoutCount,
            PICAM_ReadoutCountExists,
            PICAM_ReadoutCountRelevant,
            PicamParameter_ReadoutCount);
    piCreateAndIndexPIParam(PICAM_ReadoutRateCalcString, asynParamFloat64,
            PICAM_ReadoutRateCalc,
            PICAM_ReadoutRateCalculationExists,
            PICAM_ReadoutRateCalculationRelevant,
            PicamParameter_ReadoutRateCalculation);
    piCreateAndIndexPIParam(PICAM_ReadoutStrideString, asynParamInt32,
            PICAM_ReadoutStride,
            PICAM_ReadoutStrideExists,
            PICAM_ReadoutStrideRelevant,
            PicamParameter_ReadoutStride);
    piCreateAndIndexPIRoisParam(PICAM_RoisString,
            PICAM_RoisExists,
            PICAM_RoisRelevant,
            PicamParameter_Rois);
    piCreateAndIndexPIParam(PICAM_TimeStampBitDepthString, asynParamInt32,
            PICAM_TimeStampBitDepth,
            PICAM_TimeStampBitDepthExists,
            PICAM_TimeStampBitDepthRelevant,
            PicamParameter_TimeStampBitDepth);
    piCreateAndIndexPIParam(PICAM_TimeStampResolutionString, asynParamInt32,
            PICAM_TimeStampResolution,
            PICAM_TimeStampResolutionExists,
            PICAM_TimeStampResolutionRelevant,
            PicamParameter_TimeStampResolution);
    piCreateAndIndexPIParam(PICAM_TimeStampsString, asynParamInt32,
            PICAM_TimeStamps,
            PICAM_TimeStampsExists,
            PICAM_TimeStampsRelevant,
            PicamParameter_TimeStamps);
    piCreateAndIndexPIParam(PICAM_TrackFramesString, asynParamInt32,
            PICAM_TrackFrames,
            PICAM_TrackFramesExists,
            PICAM_TrackFramesRelevant,
            PicamParameter_TrackFrames);

    piCreateAndIndexPIParam(PICAM_CcdCharacteristicsString, asynParamOctet,
            PICAM_CcdCharacteristics,
            PICAM_CcdCharacteristicsExists,
            PICAM_CcdCharacteristicsRelevant,
            PicamParameter_CcdCharacteristics);
    piCreateAndIndexPIParam(PICAM_PixelGapHeightString, asynParamFloat64,
            PICAM_PixelGapHeight,
            PICAM_PixelGapHeightExists,
            PICAM_PixelGapHeightRelevant,
            PicamParameter_PixelGapHeight);
    piCreateAndIndexPIParam(PICAM_PixelGapWidthString, asynParamFloat64,
            PICAM_PixelGapWidth,
            PICAM_PixelGapWidthExists,
            PICAM_PixelGapHeightRelevant,
            PicamParameter_PixelGapWidth);
    piCreateAndIndexPIParam(PICAM_PixelHeightString, asynParamFloat64,
            PICAM_PixelHeight,
            PICAM_PixelHeightExists,
            PICAM_PixelGapHeightRelevant,
            PicamParameter_PixelHeight);
    piCreateAndIndexPIParam(PICAM_PixelWidthString, asynParamFloat64,
            PICAM_PixelWidth,
            PICAM_PixelWidthExists,
            PICAM_PixelWidthRelevant,
            PicamParameter_PixelWidth);
    piCreateAndIndexPIParam(PICAM_SensorActiveBottomMarginString, asynParamInt32,
            PICAM_SensorActiveBottomMargin,
            PICAM_SensorActiveBottomMarginExists,
            PICAM_SensorActiveBottomMarginRelevant,
            PicamParameter_SensorActiveBottomMargin);
    piCreateAndIndexADParam(PICAM_SensorActiveHeightString,
            ADMaxSizeY,
            PICAM_SensorActiveHeightExists,
            PICAM_SensorActiveHeightRelevant,
            PicamParameter_SensorActiveHeight);
    piCreateAndIndexPIParam(PICAM_SensorActiveLeftMarginString, asynParamInt32,
            PICAM_SensorActiveLeftMargin,
            PICAM_SensorActiveLeftMarginExists,
            PICAM_SensorActiveLeftMarginRelevant,
            PicamParameter_SensorActiveLeftMargin);
    piCreateAndIndexPIParam(PICAM_SensorActiveRightMarginString, asynParamInt32,
            PICAM_SensorActiveRightMargin,
            PICAM_SensorActiveRightMarginExists,
            PICAM_SensorActiveRightMarginRelevant,
            PicamParameter_SensorActiveRightMargin);
    piCreateAndIndexPIParam(PICAM_SensorActiveTopMarginString, asynParamInt32,
            PICAM_SensorActiveTopMargin,
            PICAM_SensorActiveTopMarginExists,
            PICAM_SensorActiveTopMarginRelevant,
            PicamParameter_SensorActiveTopMargin);
    piCreateAndIndexADParam(PICAM_SensorActiveWidthString,
            ADMaxSizeX,
            PICAM_SensorActiveWidthExists,
            PICAM_SensorActiveWidthRelevant,
            PicamParameter_SensorActiveWidth);
    piCreateAndIndexPIParam(PICAM_SensorMaskedBottomMarginString, asynParamInt32,
            PICAM_SensorMaskedBottomMargin,
            PICAM_SensorMaskedBottomMarginExists,
            PICAM_SensorMaskedBottomMarginRelevant,
            PicamParameter_SensorMaskedBottomMargin);
    piCreateAndIndexPIParam(PICAM_SensorMaskedHeightString, asynParamInt32,
            PICAM_SensorMaskedHeight,
            PICAM_SensorMaskedHeightExists,
            PICAM_SensorMaskedHeightRelevant,
            PicamParameter_SensorMaskedHeight);
    piCreateAndIndexPIParam(PICAM_SensorMaskedTopMarginString, asynParamInt32,
            PICAM_SensorMaskedTopMargin,
            PICAM_SensorMaskedTopMarginExists,
            PICAM_SensorMaskedTopMarginRelevant,
            PicamParameter_SensorMaskedTopMargin);
    piCreateAndIndexPIParam(PICAM_SensorSecondaryActiveHeightString,
            asynParamInt32,
            PICAM_SensorSecondaryActiveHeight,
            PICAM_SensorSecondaryActiveHeightExists,
            PICAM_SensorSecondaryActiveHeightRelevant,
            PicamParameter_SensorSecondaryActiveHeight);
    piCreateAndIndexPIParam(PICAM_SensorSecondaryMaskedHeightString,
            asynParamInt32,
            PICAM_SensorSecondaryMaskedHeight,
            PICAM_SensorSecondaryMaskedHeightExists,
            PICAM_SensorSecondaryMaskedHeightRelevant,
            PicamParameter_SensorSecondaryMaskedHeight);
    piCreateAndIndexPIParam(PICAM_SensorTypeString, asynParamOctet,
            PICAM_SensorType,
            PICAM_SensorTypeExists,
            PICAM_SensorTypeRelevant,
            PicamParameter_SensorType);
    //Sensor Layout
    piCreateAndIndexPIParam(PICAM_ActiveBottomMarginString, asynParamInt32,
    		PICAM_ActiveBottomMargin,
            PICAM_ActiveBottomMarginExists,
            PICAM_ActiveBottomMarginRelevant,
            PicamParameter_ActiveBottomMargin);
    piCreateAndIndexPIParam(PICAM_ActiveHeightString, asynParamInt32,
    		PICAM_ActiveHeight,
            PICAM_ActiveHeightExists,
            PICAM_ActiveHeightRelevant,
            PicamParameter_ActiveHeight);
    piCreateAndIndexPIParam(PICAM_ActiveLeftMarginString, asynParamInt32,
    		PICAM_ActiveLeftMargin,
            PICAM_ActiveLeftMarginExists,
            PICAM_ActiveLeftMarginRelevant,
            PicamParameter_ActiveLeftMargin);
    piCreateAndIndexPIParam(PICAM_ActiveRightMarginString, asynParamInt32,
    		PICAM_ActiveRightMargin,
            PICAM_ActiveRightMarginExists,
            PICAM_ActiveRightMarginRelevant,
            PicamParameter_ActiveRightMargin);
    piCreateAndIndexPIParam(PICAM_ActiveTopMarginString, asynParamInt32,
    		PICAM_ActiveTopMargin,
            PICAM_ActiveTopMarginExists,
            PICAM_ActiveTopMarginRelevant,
            PicamParameter_ActiveTopMargin);
    piCreateAndIndexPIParam(PICAM_ActiveWidthString, asynParamInt32,
    		PICAM_ActiveWidth,
            PICAM_ActiveWidthExists,
            PICAM_ActiveWidthRelevant,
            PicamParameter_ActiveWidth);
    piCreateAndIndexPIParam(PICAM_MaskedBottomMarginString, asynParamInt32,
    		PICAM_MaskedBottomMargin,
            PICAM_MaskedBottomMarginExists,
            PICAM_MaskedBottomMarginRelevant,
            PicamParameter_MaskedBottomMargin);
    piCreateAndIndexPIParam(PICAM_MaskedHeightString, asynParamInt32,
    		PICAM_MaskedHeight,
            PICAM_MaskedHeightExists,
            PICAM_MaskedHeightRelevant,
            PicamParameter_MaskedHeight);
    piCreateAndIndexPIParam(PICAM_MaskedTopMarginString, asynParamInt32,
    		PICAM_MaskedTopMargin,
            PICAM_MaskedTopMarginExists,
            PICAM_MaskedTopMarginRelevant,
            PicamParameter_MaskedTopMargin);
    piCreateAndIndexPIParam(PICAM_SecondaryActiveHeightString, asynParamInt32,
    		PICAM_SecondaryActiveHeight,
            PICAM_SecondaryActiveHeightExists,
            PICAM_SecondaryActiveHeightRelevant,
            PicamParameter_SecondaryActiveHeight);
    piCreateAndIndexPIParam(PICAM_SecondaryMaskedHeightString, asynParamInt32,
    		PICAM_SecondaryMaskedHeight,
            PICAM_SecondaryMaskedHeightExists,
            PICAM_SecondaryMaskedHeightRelevant,
            PicamParameter_SecondaryMaskedHeight);
    //Sensor Cleaning
    piCreateAndIndexPIParam(PICAM_CleanBeforeExposureString, asynParamInt32,
    		PICAM_CleanBeforeExposure,
            PICAM_CleanBeforeExposureExists,
            PICAM_CleanBeforeExposureRelevant,
            PicamParameter_CleanBeforeExposure);
    piCreateAndIndexPIParam(PICAM_CleanCycleCountString, asynParamInt32,
    		PICAM_CleanCycleCount,
            PICAM_CleanCycleCountExists,
            PICAM_CleanCycleCountRelevant,
            PicamParameter_CleanCycleCount);
    piCreateAndIndexPIParam(PICAM_CleanCycleHeightString, asynParamInt32,
    		PICAM_CleanCycleHeight,
            PICAM_CleanCycleHeightExists,
            PICAM_CleanCycleHeightRelevant,
            PicamParameter_CleanCycleHeight);
    piCreateAndIndexPIParam(PICAM_CleanSectionFinalHeightString, asynParamInt32,
    		PICAM_CleanSectionFinalHeight,
            PICAM_CleanSectionFinalHeightExists,
            PICAM_CleanSectionFinalHeightRelevant,
            PicamParameter_CleanSectionFinalHeight);
    piCreateAndIndexPIParam(PICAM_CleanSectionFinalHeightCountString, asynParamInt32,
    		PICAM_CleanSectionFinalHeightCount,
            PICAM_CleanSectionFinalHeightCountExists,
            PICAM_CleanSectionFinalHeightCountRelevant,
            PicamParameter_CleanSectionFinalHeightCount);
    piCreateAndIndexPIParam(PICAM_CleanSerialRegisterString, asynParamInt32,
    		PICAM_CleanSerialRegister,
            PICAM_CleanSerialRegisterExists,
            PICAM_CleanSerialRegisterRelevant,
            PicamParameter_CleanSerialRegister);
    piCreateAndIndexPIParam(PICAM_CleanUntilTriggerString, asynParamInt32,
    		PICAM_CleanUntilTrigger,
            PICAM_CleanUntilTriggerExists,
            PICAM_CleanUntilTriggerRelevant,
            PicamParameter_CleanUntilTrigger);

    //Sensor Temperature
    piCreateAndIndexPIParam(PICAM_DisableCoolingFanString, asynParamInt32,
            PICAM_DisableCoolingFan,
            PICAM_DisableCoolingFanExists,
            PICAM_DisableCoolingFanRelevant,
            PicamParameter_DisableCoolingFan);
    piCreateAndIndexPIParam(PICAM_EnableSensorWindowHeaterString, asynParamInt32,
            PICAM_EnableSensorWindowHeater,
            PICAM_EnableSensorWindowHeaterExists,
            PICAM_EnableSensorWindowHeaterRelevant,
            PicamParameter_EnableSensorWindowHeater);
    piCreateAndIndexADParam(PICAM_SensorTemperatureReadingString,
            ADTemperatureActual, PICAM_SensorTemperatureReadingExists,
            PICAM_SensorTemperatureReadingRelevant,
            PicamParameter_SensorTemperatureReading);
    piCreateAndIndexADParam(PICAM_SensorTemperatureSetPointString,
            ADTemperature,
            PICAM_SensorTemperatureSetPointExists,
            PICAM_SensorTemperatureSetPointRelevant,
            PicamParameter_SensorTemperatureSetPoint);
    piCreateAndIndexPIParam(PICAM_SensorTemperatureStatusString, asynParamOctet,
            PICAM_SensorTemperatureStatus,
            PICAM_SensorTemperatureStatusExists,
            PICAM_SensorTemperatureStatusRelevant,
            PicamParameter_SensorTemperatureStatus);

    // Display aids
    createParam(PICAM_EnableROIMinXInputString, asynParamInt32,
    		&PICAM_EnableROIMinXInput);
    createParam(PICAM_EnableROISizeXInputString, asynParamInt32,
    		&PICAM_EnableROISizeXInput);
    createParam(PICAM_EnableROIMinYInputString, asynParamInt32,
    		&PICAM_EnableROIMinYInput);
    createParam(PICAM_EnableROISizeYInputString, asynParamInt32,
    		&PICAM_EnableROISizeYInput);

    status = setStringParam(ADManufacturer, "Princeton Instruments");
    status |= setStringParam(ADModel, "Not Connected");
    status |= setIntegerParam(NDArraySize, 0);
    status |= setIntegerParam(NDDataType, NDUInt16);

    status |= setStringParam(PICAM_VersionNumber, emptyStr);
    status |= setIntegerParam(PICAM_AvailableCameras, 0);
    status |= setStringParam(PICAM_CameraInterface, emptyStr);
    status |= setStringParam(PICAM_SensorName, emptyStr);
    status |= setStringParam(PICAM_SerialNumber, emptyStr);
    status |= setStringParam(PICAM_FirmwareRevision, emptyStr);
    status |= setIntegerParam(PICAM_UnavailableCameras, 0);
    status |= setStringParam(PICAM_CameraInterfaceUnavailable,
            emptyStr);
    status |= setStringParam(PICAM_SensorNameUnavailable, emptyStr);
    status |= setStringParam(PICAM_SerialNumberUnavailable, emptyStr);
    status |= setStringParam(PICAM_FirmwareRevisionUnavailable, emptyStr);
    status |= setIntegerParam(ADNumImagesCounter, 1);
    status |= setIntegerParam(PICAM_EnableROIMinXInput, enableDisplay);
    status |= setIntegerParam(PICAM_EnableROISizeXInput, enableDisplay);
    status |= setIntegerParam(PICAM_EnableROIMinYInput, enableDisplay);
    status |= setIntegerParam(PICAM_EnableROISizeYInput, enableDisplay);
    callParamCallbacks();

    piLoadAvailableCameraIDs();
    setIntegerParam(PICAM_AvailableCameras, 0);
    callParamCallbacks();

    piLoadUnavailableCameraIDs();
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: unable to set camera parameters\n", driverName,
                functionName);
        return;
    }
    piHandleNewImageEvent = epicsEventCreate(epicsEventEmpty);
    /* Create the thread that updates the images */
    status = (epicsThreadCreate("piHandleNewImageTaskC",
                                epicsThreadPriorityMedium,
                                epicsThreadGetStackSize(epicsThreadStackMedium),
                                (EPICSTHREADFUNC)piHandleNewImageTaskC,
                                this) == NULL);
    initializeDetector();

    epicsAtExit(exitCallbackC, this);
}

/**
 * Destructor function.  Clear out Picam Library
 */
ADPICam::~ADPICam() {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s Enter\n",
            driverName,
            __func__);
    int status = asynSuccess;

    Picam_StopAcquisition(currentCameraHandle);
    imageThreadKeepAlive = false;
    PicamAdvanced_UnregisterForAcquisitionUpdated(currentDeviceHandle,
                    piAcquistionUpdated);
    status |= piUnregisterRelevantWatch(currentCameraHandle);
    status |= piUnregisterValueChangeWatch(currentCameraHandle);
    Picam_UninitializeLibrary();
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s Exit\n",
            driverName,
            __func__);
}

/**
 * Initialize Picam property initialization
 * - Read PICAM Library version
 * - Register camera discovery callback
 * - Start camera discovery
 */
asynStatus ADPICam::initializeDetector() {
    piint versionMajor, versionMinor, versionDistribution, versionReleased;

    static const char* functionName = "initializeDetector";
    const char *errorString;
    //bool retVal = true;
    char picamVersion[16];
    int status = asynSuccess;
    PicamError error;

    // Read PICAM Library version #
    Picam_GetVersion(&versionMajor, &versionMinor, &versionDistribution,
            &versionReleased);
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Initialized PICam Version %d.%d.%d.%d\n", driverName,
            functionName, versionMajor, versionMinor, versionDistribution,
            versionReleased);
    sprintf(picamVersion, "%d.%d.%d.%d", versionMajor, versionMinor,
            versionDistribution, versionReleased);
    status |= setStringParam(PICAM_VersionNumber, (const char *) picamVersion);
    status |= setStringParam(ADSDKVersion, (const char*) picamVersion);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: unable to set camera parameters\n", driverName,
                functionName);
        return asynError;
    }
    // Register callback for Camera Discovery
    error = PicamAdvanced_RegisterForDiscovery(piCameraDiscovered);
    if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                &errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Fail to Register for Camera Discovery :%s\n", driverName,
                functionName, errorString);
        return asynError;
    }
    // Launch camera discovery process
    error = PicamAdvanced_DiscoverCameras();
    if (error != PicamError_None) {

        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Fail to start camera discovery :%s\n", driverName,
                functionName, errorString);
        return asynError;
    }

    return (asynStatus) status;
}

/**
 * Override method from asynPortDriver to populate pull-down lists
 * for camera parameters.  Note that for this to work asynEnumMask must be set
 * when calling ADDriver constructor
 */
asynStatus ADPICam::readEnum(asynUser *pasynUser, char *strings[], int values[],
        int severities[], size_t nElements, size_t *nIn) {
    static const char *functionName = "readEnum";
    int status = asynSuccess;
    char enumString[64];
    const char *modelString;
    //const char *NAString = "N.A. 0";
    PicamParameter picamParameter;
    int function = pasynUser->reason;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "*******%s:%s: entry\n",
            driverName, functionName);

    *nIn = 0;
    if (function == PICAM_AvailableCameras) {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "Getting Available IDs\n");
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s availableCamerasCount %d\n", driverName, functionName,
                availableCamerasCount);
        for (int ii = 0; ii < availableCamerasCount; ii++) {
            Picam_GetEnumerationString(PicamEnumeratedType_Model,
                    (piint) availableCameraIDs[ii].model, &modelString);
            pibln camConnected = false;
            Picam_IsCameraIDConnected(availableCameraIDs, &camConnected);
            sprintf(enumString, "%s", modelString);
            asynPrint(pasynUser, ASYN_TRACE_FLOW,
                    "\n%s:%s: \nCamera[%d]\n---%s\n---%d\n---%s\n---%s\n",
                    driverName, functionName, ii, modelString,
                    availableCameraIDs[ii].computer_interface,
                    availableCameraIDs[ii].sensor_name,
                    availableCameraIDs[ii].serial_number);
            if (strings[*nIn]) {
                free(strings[*nIn]);
            }

            Picam_DestroyString(modelString);
            strings[*nIn] = epicsStrDup(enumString);
            values[*nIn] = ii;
            severities[*nIn] = 0;
            (*nIn)++;
        }

    } else if (function == PICAM_UnavailableCameras) {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "Getting Unavailable IDs\n");
        for (int ii = 0; ii < unavailableCamerasCount; ii++) {
            Picam_GetEnumerationString(PicamEnumeratedType_Model,
                    (piint) unavailableCameraIDs[ii].model, &modelString);
            sprintf(enumString, "%s", modelString);
            asynPrint(pasynUser, ASYN_TRACE_FLOW,
                    "\n%s:%s: \nCamera[%d]\n---%s\n---%d\n---%s\n---%s\n",
                    driverName, functionName, ii, modelString,
                    unavailableCameraIDs[ii].computer_interface,
                    unavailableCameraIDs[ii].sensor_name,
                    unavailableCameraIDs[ii].serial_number);
            if (strings[*nIn]) {
                free(strings[*nIn]);
            }

            Picam_DestroyString(modelString);
            strings[*nIn] = epicsStrDup(enumString);
            values[*nIn] = ii;
            severities[*nIn] = 0;
            (*nIn)++;
        }
    } else if ( piLookupPICamParameter(function, picamParameter) ==
            PicamError_None) {
        piGenerateListValuesFromCollection(pasynUser,
                strings, values, severities, nIn,
                function, picamParameter);
    } else {
        return ADDriver::readEnum(pasynUser, strings, values, severities,
                nElements, nIn);
    }
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s: error calling enum functions, status=%d\n", driverName,
                functionName, status);
    }
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: exit\n", driverName,
            functionName);
    return asynSuccess;
}

/**
 * Read String information for fields with Enumeration type and no
 * constraint type.
 */
asynStatus ADPICam::readOctet(asynUser *pasynUser, char *value,
                                    size_t nChars, size_t *nActual,
									int *eomReason)
{
	static const char *functionName = "readOctet";
	int status = asynSuccess;
	pibln parameterDoesExist;
	pibln parameterRelevant;
	piint intValue;
	int function = pasynUser->reason;
	PicamParameter picamParameter;
	PicamValueType valueType;
	PicamEnumeratedType enumType;
	PicamError error;
	const char *errString;
	const char *enumString;

	if ( piLookupPICamParameter(function, picamParameter) == PicamError_None) {
		Picam_DoesParameterExist(currentCameraHandle,
				picamParameter,
				&parameterDoesExist);
		if (parameterDoesExist){
			error = Picam_IsParameterRelevant(currentCameraHandle,
					picamParameter,
					&parameterRelevant);
		}
		else{
            strncpy (value, "", 1);
            value[nChars-1] = '\0';
            *nActual = strlen(value);
            return asynSuccess;

		}
		if (error != PicamError_None){
			Picam_GetEnumerationString(PicamEnumeratedType_Error,
					error, &errString);
			asynPrint(pasynUser, ASYN_TRACE_ERROR,
					"%s:%s Trouble determining if parameter is relevant: %s\n",
					driverName, functionName, errString);
			return asynError;
		}
		if (parameterRelevant){
			Picam_GetParameterValueType(currentCameraHandle,
					picamParameter,
					&valueType);
			switch (valueType) {
			case PicamValueType_Enumeration:
				Picam_GetParameterEnumeratedType(currentCameraHandle,
						picamParameter,
						&enumType);
				Picam_GetParameterIntegerValue(currentCameraHandle,
						picamParameter,
						&intValue);
				Picam_GetEnumerationString(enumType, intValue, &enumString);
				asynPrint (pasynUserSelf, ASYN_TRACE_FLOW,
						"%s:%s ----readOctet value=%s\n",
						driverName,
						functionName,
						enumString);
				strncpy (value, enumString, nChars);
				value[nChars-1] = '\0';
				*nActual = strlen(value);
				Picam_DestroyString(enumString);
				break;
			default:
			  break;
			}
		}
	}
	else  {
        /* If this parameter belongs to a base class call its method */
        if (function < PICAM_FIRST_PARAM) {
            status = ADDriver::readOctet(pasynUser, value, nChars, nActual,
            		eomReason);
        }
	}

	return (asynStatus)status;
}

/**
 * Overload method for asynPortDriver's report method
 */
void ADPICam::report(FILE *fp, int details) {
    static const char *functionName = "report";
    PicamError error = PicamError_None;
    pibln picamInitialized;
    const char *modelString;
    char enumString[64];
    const PicamRoisConstraint *roisConstraints;
    const char *errorString;

    fprintf(fp, "############ %s:%s ###############\n", driverName,
            functionName);
    Picam_IsLibraryInitialized(&picamInitialized);
    if (!picamInitialized) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s: Error: Picam is not initialized!\n", driverName,
                functionName);
        return;

    }
    char versionNumber[40];
    getStringParam(PICAM_VersionNumber, 40, versionNumber);
    fprintf(fp, "%s:%s Initialized PICam Version %s\n", driverName,
            functionName, versionNumber);
    fprintf(fp, "----------------\n");
    fprintf(fp, "%s:%s availableCamerasCount %d\n", driverName, functionName,
            availableCamerasCount);
    for (int ii = 0; ii < availableCamerasCount; ii++) {
        fprintf(fp, "Available Camera[%d]\n", ii);
        Picam_GetEnumerationString(PicamEnumeratedType_Model,
                (piint) availableCameraIDs[ii].model, &modelString);
        sprintf(enumString, "%s", modelString);
        fprintf(fp, "\n---%s\n---%d\n---%s\n---%s\n", modelString,
                availableCameraIDs[ii].computer_interface,
                availableCameraIDs[ii].sensor_name,
                availableCameraIDs[ii].serial_number);
    }
    fprintf(fp, "----------------\n");
    fprintf(fp, "%s:%s unavailableCamerasCount %d\n", driverName, functionName,
            unavailableCamerasCount);
    for (int ii = 0; ii < unavailableCamerasCount; ii++) {
        fprintf(fp, "Unavailable Camera[%d]\n", ii);
        Picam_GetEnumerationString(PicamEnumeratedType_Model,
                (piint) unavailableCameraIDs[ii].model, &modelString);
        sprintf(enumString, "%s", modelString);
        fprintf(fp, "\n---%s\n---%d\n---%s\n---%s\n", modelString,
                unavailableCameraIDs[ii].computer_interface,
                unavailableCameraIDs[ii].sensor_name,
                unavailableCameraIDs[ii].serial_number);
    }
    fprintf(fp, "----------------\n");

    if (details > 7) {
        fprintf(fp, "--------------------\n");
        fprintf(fp, " Parameters\n");
        const PicamParameter *parameterList;
        PicamConstraintType constraintType;
        piint parameterCount;
        Picam_GetParameters(currentCameraHandle, &parameterList,
                &parameterCount);
        for (int ii = 0; ii < parameterCount; ii++) {
            const char *parameterName;
            Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                    parameterList[ii], &parameterName);

            fprintf(fp, "param[%d] %s\n", parameterList[ii], parameterName);
            Picam_GetParameterConstraintType(currentCameraHandle,
                    parameterList[ii], &constraintType);
            switch (constraintType) {
            case PicamConstraintType_Range:
                const PicamRangeConstraint *constraint;
                Picam_GetParameterRangeConstraint(currentCameraHandle,
                        parameterList[ii], PicamConstraintCategory_Capable,
                        &constraint);
                fprintf(fp, "--- Range Constraint\n");
                fprintf(fp, "------Empty Set %d\n", constraint->empty_set);
                fprintf(fp, "------Minimum %f\n", constraint->minimum);
                fprintf(fp, "------Maximum %f\n", constraint->maximum);
                fprintf(fp, "------%d excluded values\n--------",
                        constraint->excluded_values_count);
                for (int ec = 0; ec < constraint->excluded_values_count; ec++) {
                    fprintf(fp, "%f, ", constraint->excluded_values_array[ec]);
                }
                fprintf(fp, "\n");
                fprintf(fp, "------%d outlying values\n--------",
                        constraint->outlying_values_count);
                for (int ec = 0; ec < constraint->outlying_values_count; ec++) {
                    fprintf(fp, "%f, ", constraint->outlying_values_array[ec]);
                }
                fprintf(fp, "\n");
                Picam_DestroyRangeConstraints(constraint);
                break;
            case PicamConstraintType_Rois:
                error = Picam_GetParameterRoisConstraint(currentCameraHandle,
                        PicamParameter_Rois, PicamConstraintCategory_Required,
                        &roisConstraints);
                if (error != PicamError_None) {
                    Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                            &errorString);
                    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                            "%s:%s Error retrieving rois constraints %s\n",
                            driverName, functionName, errorString);
                    Picam_DestroyString(errorString);
                }
                fprintf(fp, "--- ROI Constraints\n"
                        "----- X min %f, X max %f\n"
                        "----- Y min %f, Y max %f\n"
                        "----- width min %f,  width max %f\n"
                        "----- height %f, height max %f\n"
                        "----- Rules 0x%x\n",
                        //			"--X bin min %d, X bin max %d\n"
                        //			"--Y bin min %d, Y bin max %d\n",
                        roisConstraints->x_constraint.minimum,
                        roisConstraints->x_constraint.maximum,
                        roisConstraints->y_constraint.minimum,
                        roisConstraints->y_constraint.maximum,
                        roisConstraints->width_constraint.minimum,
                        roisConstraints->width_constraint.maximum,
                        roisConstraints->height_constraint.minimum,
                        roisConstraints->height_constraint.maximum,
                        roisConstraints->rules);
				fprintf(fp, "-----x_binning_limits:\n");
				for (int jj = 0; jj < roisConstraints->x_binning_limits_count; jj++) {
					fprintf(fp, "------ %d\n",
							roisConstraints->x_binning_limits_array[jj]);
				}
				fprintf(fp, "-----y_binning_limits:\n");
				for (int kk = 0; kk < roisConstraints->y_binning_limits_count; kk++) {
					fprintf(fp, "------ %d\n",
							roisConstraints->y_binning_limits_array[kk]);
				}

                //			roisConstraints->x_.minimum,
                //			roisConstraints->x_constraint.minimum);
                Picam_DestroyRoisConstraints(roisConstraints);
                break;
            case PicamConstraintType_Collection:
                fprintf(fp, "----Collection Constraints\n");
                const PicamCollectionConstraint *collectionConstraint;
                Picam_GetParameterCollectionConstraint(currentCameraHandle,
                        parameterList[ii], PicamConstraintCategory_Capable,
                        &collectionConstraint);
                PicamValueType valType;
                Picam_GetParameterValueType(currentCameraHandle,
                        parameterList[ii], &valType);
                switch (valType) {
                case PicamValueType_Enumeration:
                    for (int cc = 0; cc < collectionConstraint->values_count;
                            cc++) {
                        PicamEnumeratedType enumeratedType;
                        const pichar *enumerationString;
                        Picam_GetParameterEnumeratedType(currentCameraHandle,
                                parameterList[ii], &enumeratedType);
                        Picam_GetEnumerationString(enumeratedType,
                                (int) collectionConstraint->values_array[cc],
                                &enumerationString);
                        fprintf(fp, "------ %s\n", enumerationString);
                        Picam_DestroyString(enumerationString);
                    }
                    break;
                case PicamValueType_FloatingPoint:
                    for (int cc = 0; cc < collectionConstraint->values_count;
                            cc++) {
                        fprintf(fp, "------ %f\n",
                                collectionConstraint->values_array[cc]);
                    }
                    break;
                case PicamValueType_Integer:
                    for (int cc = 0; cc < collectionConstraint->values_count;
                            cc++) {
                        fprintf(fp, "------ %d\n",
                                (piint) collectionConstraint->values_array[cc]);
                    }
                    break;
                case PicamValueType_LargeInteger:
                    for (int cc = 0; cc < collectionConstraint->values_count;
                            cc++) {
                        fprintf(fp, "------ %ld\n",
                                (pi64s) collectionConstraint->values_array[cc]);
                    }
                    break;
                case PicamValueType_Boolean:
                    for (int cc = 0; cc < collectionConstraint->values_count;
                            cc++) {
                        fprintf(fp, "------ %s\n",
                                collectionConstraint->values_array[cc] ?
                                        "true" : "false");
                    }
                    break;
                default:
                    fprintf(fp,
                            "---Unhandled valueType for collection constraint");
                }
                Picam_DestroyCollectionConstraints(collectionConstraint);
                break;
            default:
                break;
            }
        }
    }

    if (details > 20) {
        piint demoModelCount;
        const PicamModel * demoModels;

        error = Picam_GetAvailableDemoCameraModels(&demoModels,
                &demoModelCount);
        for (int ii = 0; ii < demoModelCount; ii++) {
            const char* modelString;
            error = Picam_GetEnumerationString(PicamEnumeratedType_Model,
                    demoModels[ii], &modelString);
            fprintf(fp, "demoModel[%d]: %s\n", ii, modelString);
            Picam_DestroyString(modelString);
        }
        Picam_DestroyModels(demoModels);
    }
    ADDriver::report(fp, details);
}

/**
 * Overload asynPortDriver's writeFloat64 to handle driver specific
 * parameters.
 */
asynStatus ADPICam::writeFloat64(asynUser *pasynUser, epicsFloat64 value) {
    static const char *functionName = "writeFloat64";
    int status = asynSuccess;
    PicamError error = PicamError_None;
    const char *errorString;
    int function = pasynUser->reason;
    PicamParameter picamParameter;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: entry\n", driverName,
            functionName);

    // Make sure that we write the value to the param.  This may get changed
    // at a later stage
    setDoubleParam(function, value);

    if (piLookupPICamParameter(function, picamParameter) ==
    		PicamError_None){
    	pibln isRelevant;
    	error = Picam_IsParameterRelevant(currentCameraHandle,
    			picamParameter,
				&isRelevant);
    	if (error == PicamError_ParameterDoesNotExist){
    		isRelevant = false;
    	}
    	else if (error != PicamError_None) {
    		Picam_GetEnumerationString(PicamEnumeratedType_Error,
    				error,
					&errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Trouble getting parameter associated with driver"
                    " param %d, picam param:%d: %s\n",
                    driverName,
                    functionName,
                    function,
                    picamParameter,
                    errorString);
            Picam_DestroyString(errorString);
            return asynError;
    	}
    	if (isRelevant && currentCameraHandle != NULL) {
    		PicamConstraintType constraintType;
    		error = Picam_GetParameterConstraintType(currentCameraHandle,
    				picamParameter,
					&constraintType);
            if (error != PicamError_None){
                Picam_GetEnumerationString(PicamEnumeratedType_Error,
                        error,
                        &errorString);
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                        "%s:%s Trouble getting constraint type assocoated with "
                        "driver param %d, picam param:%d: %s\n",
                        driverName,
                        functionName,
                        function,
                        picamParameter,
                        errorString);
                Picam_DestroyString(errorString);
            }
            if (constraintType==PicamConstraintType_Range) {
                status |= piWriteFloat64RangeType(pasynUser,
                        value,
                        function,
                        picamParameter);
            }
            else if (constraintType == PicamConstraintType_Collection) {
				error = Picam_SetParameterFloatingPointValue(currentCameraHandle,
						picamParameter, (piflt) value);
				if (error != PicamError_None) {
					Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
							&errorString);

					asynPrint(pasynUser, ASYN_TRACE_ERROR,
							"%s:%s error writing Float64 value to %f\n"
							"Reason %s\n",
							driverName,
							functionName,
							value,
							errorString);
					Picam_DestroyString(errorString);
					return asynError;
				}

            }
            else if (constraintType==PicamConstraintType_None) {
                status |= Picam_SetParameterFloatingPointValue(
                		currentCameraHandle,
						picamParameter,
						value);
            }
    	}
    } else {
        /* If this parameter belongs to a base class call its method */
        if (function < PICAM_FIRST_PARAM) {
            status = ADDriver::writeFloat64(pasynUser, value);
        }

    }
    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();

    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s:%s: error, status=%d function=%d, value=%f\n", driverName,
                functionName, status, function, value);
    }
    else {
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
                "%s:%s: function=%d, value=%f\n", driverName, functionName,
                function, value);
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s: exit\n", driverName,
            functionName);
    }
    return (asynStatus) status;

}

/**
 * Override asynPortDriver's writeInt32 method.
 */
asynStatus ADPICam::writeInt32(asynUser *pasynUser, epicsInt32 value) {
    static const char *functionName = "writeInt32";
    int status = asynSuccess;
    PicamError error = PicamError_None;
    const char* errorString;
    int sizeX, sizeY, binX, binY, minX, minY;
    int function = pasynUser->reason;
    PicamParameter picamParameter;
    int adStatus;
    int acquiring;

    // Record status and acquire for use later
    getIntegerParam(ADStatus, &adStatus);
    getIntegerParam(ADAcquire, &acquiring);

    // Make sure that we write the value to the param.  This may get changed
    // at a later stage
    setIntegerParam(function, value);


    if (function == PICAM_AvailableCameras) {
        status |= piSetSelectedCamera(pasynUser, (int) value);
        status |= piSetParameterValuesFromSelectedCamera();
    } else if (function == PICAM_UnavailableCameras) {
        piSetSelectedUnavailableCamera(pasynUser, (int) value);
    } else if (piLookupPICamParameter(function, picamParameter) ==
            PicamError_None) {
        pibln isRelevant;
        error = Picam_IsParameterRelevant(currentCameraHandle,
                picamParameter,
                &isRelevant);
        if (error == PicamError_ParameterDoesNotExist) {
            isRelevant = false;
        }
        else if (error != PicamError_None) {
                Picam_GetEnumerationString(PicamEnumeratedType_Error,
                    error,
                    &errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Trouble getting parameter associated with driver"
                    " param %d, picam param:%d: %s\n",
                    driverName,
                    functionName,
                    function,
                    picamParameter,
                    errorString);
            Picam_DestroyString(errorString);
            return asynError;
        }
        if (isRelevant && currentCameraHandle != NULL){
            PicamConstraintType constraintType;
            error = Picam_GetParameterConstraintType(currentCameraHandle,
                    picamParameter,
                    &constraintType);
            if (error != PicamError_None){
                Picam_GetEnumerationString(PicamEnumeratedType_Error,
                        error,
                        &errorString);
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                        "%s:%s Trouble getting constraint type assocoated with "
                        "driver param %d, picam param:%d: %s\n",
                        driverName,
                        functionName,
                        function,
                        picamParameter,
                        errorString);
                Picam_DestroyString(errorString);
                return asynError;
            }

            if (constraintType==PicamConstraintType_Range) {
                status |= piWriteInt32RangeType(pasynUser,
                        value,
                        function,
                        picamParameter);

            }
            else if (constraintType==PicamConstraintType_Collection) {
                status |= piWriteInt32CollectionType(pasynUser,
                        value,
                        function,
                        picamParameter);
            }
        }
    }
    // AD parameters for ROI size & Bin size need to be mapped into
    // PICAM's PicamRois object.
    else if (function == ADSizeX) {
        int xstatus = asynSuccess;
        sizeX = value;
        xstatus |= getIntegerParam(ADSizeY, &sizeY);
        xstatus |= getIntegerParam(ADBinX, &binX);
        xstatus |= getIntegerParam(ADBinY, &binY);
        xstatus |= getIntegerParam(ADMinX, &minX);
        xstatus |= getIntegerParam(ADMinY, &minY);
        xstatus |= piSetRois(minX, minY, sizeX, sizeY, binX, binY);
    } else if (function == ADSizeY) {
        int xstatus = asynSuccess;
        xstatus |= getIntegerParam(ADSizeX, &sizeX);
        sizeY = value;
        xstatus |= getIntegerParam(ADBinX, &binX);
        xstatus |= getIntegerParam(ADBinY, &binY);
        xstatus |= getIntegerParam(ADMinX, &minX);
        xstatus |= getIntegerParam(ADMinY, &minY);
        xstatus |= piSetRois(minX, minY, sizeX, sizeY, binX, binY);
    } else if (function == ADBinX) {
        int xstatus = asynSuccess;
        xstatus |= getIntegerParam(ADSizeX, &sizeX);
        xstatus |= getIntegerParam(ADSizeY, &sizeY);
        binX = value;
        xstatus |= getIntegerParam(ADBinY, &binY);
        xstatus |= getIntegerParam(ADMinX, &minX);
        xstatus |= getIntegerParam(ADMinY, &minY);
        xstatus |= piSetRois(minX, minY, sizeX, sizeY, binX, binY);
    } else if (function == ADBinY) {
        int xstatus = asynSuccess;
        xstatus |= getIntegerParam(ADSizeX, &sizeX);
        xstatus |= getIntegerParam(ADSizeY, &sizeY);
        xstatus |= getIntegerParam(ADBinX, &binX);
        binY = value;
        xstatus |= getIntegerParam(ADMinX, &minX);
        xstatus |= getIntegerParam(ADMinY, &minY);
        xstatus |= piSetRois(minX, minY, sizeX, sizeY, binX, binY);
    } else if (function == ADMinX) {
        int xstatus = asynSuccess;
        xstatus |= getIntegerParam(ADSizeX, &sizeX);
        xstatus |= getIntegerParam(ADSizeY, &sizeY);
        xstatus |= getIntegerParam(ADBinX, &binX);
        xstatus |= getIntegerParam(ADBinY, &binY);
        minX = value;
        xstatus |= getIntegerParam(ADMinY, &minY);
        xstatus |= piSetRois(minX, minY, sizeX, sizeY, binX, binY);
    } else if (function == ADMinY) {
        int xstatus = asynSuccess;
        xstatus |= getIntegerParam(ADSizeX, &sizeX);
        xstatus |= getIntegerParam(ADSizeY, &sizeY);
        xstatus |= getIntegerParam(ADBinX, &binX);
        xstatus |= getIntegerParam(ADBinY, &binY);
        xstatus |= getIntegerParam(ADMinX, &minX);
        minY = value;
        xstatus |= piSetRois(minX, minY, sizeX, sizeY, binX, binY);
    } else if (function == ADAcquire) {
        if (value && !acquiring){
            piAcquireStart();
        }
        else if (!value && acquiring){
            piAcquireStop();
        }
    } else {
        /* If this parameter belongs to a base class call its method */
        if (function < PICAM_FIRST_PARAM) {
            status = ADDriver::writeInt32(pasynUser, value);
        }
    }

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();

    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s:%s: error, status=%d function=%d, value=%d\n", driverName,
                functionName, status, function, value);
    }
    else {
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
                "%s:%s: function=%d, value=%d\n", driverName, functionName,
                function, value);
    }

    return (asynStatus) status;

}

/**
 * Internal method called when the Acquire button is pressed.
 */
asynStatus ADPICam::piAcquireStart(){
    const char *functionName = "piAcquireStart";
    int status = asynSuccess;
    PicamError error = PicamError_None;
    int imageMode;
    int presetImages;
    int numX;
    int numY;

    // Reset the number of Images Collected
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Enter\n",
            driverName,
            __func__);
    lock();
    setIntegerParam(ADStatus, ADStatusInitializing);
    // reset Image counter
    setIntegerParam(ADNumImagesCounter, 0);
    callParamCallbacks();
    unlock();
    getIntegerParam(ADImageMode, &imageMode);

    /* Get Image size for use by acquisition handling*/
    getIntegerParam(ADSizeX, &numX);
    getIntegerParam(ADSizeY, &numY);
    imageDims[0] = numX;
    imageDims[1] = numY;

    /* get data type for acquistion processing */
    piint pixelFormat;
    Picam_GetParameterIntegerDefaultValue(currentCameraHandle,
            PicamParameter_PixelFormat,
            &pixelFormat);
    switch(pixelFormat){
    case PicamPixelFormat_Monochrome16Bit:
        imageDataType = NDUInt16;
        break;
    case PicamPixelFormat_Monochrome32Bit:
        imageDataType = NDUInt32;
        break;
    default:
        imageDataType = NDUInt16;
        const char *pixelFormatString;
        Picam_GetEnumerationString(PicamEnumeratedType_PixelFormat,
                pixelFormat, &pixelFormatString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Unknown data type setting to NDUInt16: %s\n",
                driverName, functionName, pixelFormatString);
        Picam_DestroyString(pixelFormatString);
    }


    switch(imageMode) {
    case ADImageSingle:
        presetImages = 1;
        break;
    case ADImageMultiple:
        getIntegerParam(ADNumImages, &presetImages);
        break;
    case ADImageContinuous:
        presetImages = 0;
        break;

    }

    pi64s largePreset;
    largePreset = presetImages;
    Picam_SetParameterLargeIntegerValue(currentCameraHandle,
            PicamParameter_ReadoutCount,
            largePreset);
    int readoutStride;
    double onlineReadoutRate;
    int timeStampsUsed;

    Picam_GetParameterIntegerValue(currentCameraHandle,
    		PicamParameter_ReadoutStride,
			&readoutStride);
    Picam_GetParameterFloatingPointValue(currentCameraHandle,
    		PicamParameter_OnlineReadoutRateCalculation,
			&onlineReadoutRate);
    Picam_GetParameterIntegerValue(currentCameraHandle,
    		PicamParameter_TimeStamps,
			&timeStampsUsed);
    pi64s readouts =
    		static_cast<pi64s>(std::ceil(std::max(6.*onlineReadoutRate, 6.)));
    buffer_.resize(readouts * (readoutStride+3*8));
    PicamAcquisitionBuffer piBuffer;
    piBuffer.memory = &buffer_[0];
    piBuffer.memory_size = buffer_.size();

    error = PicamAdvanced_SetAcquisitionBuffer(currentDeviceHandle, &piBuffer);
    if (error != PicamError_None) {
        const char *errorString;
        Picam_GetEnumerationString(PicamEnumeratedType_Error,
                error,
                &errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Error Setting accquisition buffer with size %lu: %s\n",
                driverName,
                functionName,
                buffer_.size(),
                errorString);
        Picam_DestroyString(errorString);
        setIntegerParam(ADAcquire, 0);
        setIntegerParam(ADStatus, ADStatusError);
        return asynError;
    }

    lock();
    setIntegerParam(ADStatus, ADStatusAcquire);
    callParamCallbacks();
    unlock();
    const PicamParameter *failedParameterArray;
    piint failedParameterCount;
    error = Picam_CommitParameters(currentCameraHandle,
            &failedParameterArray,
            &failedParameterCount);
    if (error != PicamError_None) {
        const char *errorString;
        Picam_GetEnumerationString(PicamEnumeratedType_Error,
                error,
                &errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Error with Picam_CommitParameters: %s\n",
                driverName,
                functionName,
                errorString);
        const char *paramName;
        for (int ii=0; ii<failedParameterCount; ii++) {
            Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                    failedParameterArray[ii],
                    &paramName);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Failed to commit parameter %s\n",
                    driverName,
                    __func__,
                    paramName);
            Picam_DestroyString(paramName);
        }
        Picam_DestroyString(errorString);
        setIntegerParam(ADAcquire, 0);
        setIntegerParam(ADStatus, ADStatusError);
        return asynError;
    }
    error = Picam_StartAcquisition(currentCameraHandle);
    if (error != PicamError_None){
        const char *errorString;
        Picam_GetEnumerationString(PicamEnumeratedType_Error,
                error,
                &errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Error with Picam_StartAcquisition: %s\n",
                driverName,
                functionName,
                errorString);
        Picam_DestroyString(errorString);

        setIntegerParam(ADAcquire, 0);
        setIntegerParam(ADStatus, ADStatusError);
        return asynError;
    }

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Exit\n",
            driverName,
            __func__);
    return (asynStatus)status;
}

/**
 * Internal method called when stop acquire is pressed.
 */
asynStatus ADPICam::piAcquireStop(){
    int status = asynSuccess;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Enter\n",
            driverName, __func__);

    pibln isRunning = false;
    Picam_IsAcquisitionRunning(currentCameraHandle, &isRunning);
    if (isRunning) {
        Picam_StopAcquisition(currentDeviceHandle);
    }
    status = setIntegerParam(ADStatus, ADStatusIdle);
    if (status != asynSuccess) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "Problem setting acquire to stop\n");
    }
    status = callParamCallbacks();
    if (status != asynSuccess) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "Problem setting acquire to stop\n");

    }
    status = setIntegerParam(ADAcquire, 0);
    if (status != asynSuccess) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "Problem setting acquire to stop\n");

    }
    status = callParamCallbacks();
    if (status != asynSuccess) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "Problem setting acquire to stop\n");

    }
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Exit\n",
            driverName, __func__);
    return (asynStatus)status;

}

/**
 * Callback method for acquisition Upadated event.  This will call
 * piHandleAcquisitionUpdated ASAP.
 */
PicamError PIL_CALL ADPICam::piAcquistionUpdated(
        PicamHandle device,
        const PicamAvailableData *available,
        const PicamAcquisitionStatus *acqStatus)
{
    PicamError error = PicamError_None;
    asynStatus status;
    //const char *functionName = "piParameterIntegerValueChanged";
    //ADPICam_Instance->piSetAcquisitionData(device, available, acqStatus );
    status = ADPICam_Instance->piHandleAcquisitionUpdated(device,
            available, acqStatus);

    return error;
}

/**
 * Local Method used to add a Demo camera to the list of available
 * cameras.  This method is called by wrapper method PICamAddDemoCamera
 * which can be called from the iocsh.
 */
asynStatus ADPICam::piAddDemoCamera(const char *demoCameraName) {
    const char * functionName = "piAddDemoCamera";
    int status = asynSuccess;
    PicamError error = PicamError_None;
    const PicamModel *demoModels;
    piint demoModelCount;
    bool modelFoundInList = false;
    PicamCameraID demoID;
    const char *errorString;

    pibln libInitialized = true;
    error = Picam_IsLibraryInitialized(&libInitialized);

    if (libInitialized) {
        error = Picam_GetAvailableDemoCameraModels(&demoModels,
                &demoModelCount);
        if (error != PicamError_None) {
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
					&errorString);
        	asynPrint(ADPICam_Instance->pasynUserSelf, ASYN_TRACE_ERROR,
        			"%s:%s Trouble getting list of available Demo Cameras.  "
        			"%s\n",
					driverName,
					functionName,
					errorString);
        	Picam_DestroyString(errorString);
        	return asynError;
        }
        for (int ii = 0; ii < demoModelCount; ii++) {
            const char* modelString;
            error = Picam_GetEnumerationString(PicamEnumeratedType_Model,
                    demoModels[ii], &modelString);
            if (strcmp(demoCameraName, modelString) == 0) {
                modelFoundInList = true;
                error = Picam_ConnectDemoCamera(demoModels[ii], "ADDemo",
                        &demoID);
                if (error == PicamError_None) {
                    printf("%s:%s Adding camera demoCamera[%d] %s\n",
                            driverName, functionName, demoModels[ii],
                            demoCameraName);
                    //ADPICam_Instance->piUpdateAvailableCamerasList();
                } else {
                    Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                            &errorString);
                    printf("%s:%s Error Adding camera demoCamera[%d] %s, %s\n",
                            driverName, functionName, ii, demoCameraName,
                            errorString);
                    Picam_DestroyString(errorString);
                    status = (asynStatus)asynError;
                }
                ii = demoModelCount;
            }
            Picam_DestroyString(modelString);
        }
        Picam_DestroyModels(demoModels);
    } else {
    	Picam_GetEnumerationString(PicamEnumeratedType_Error,
    			error,
				&errorString);
    	asynPrint(ADPICam_Instance->pasynUserSelf, ASYN_TRACE_ERROR,
    			"%s%s PICAM is not initialized.  Cannot add a camera. %s\n",
				driverName,
				functionName,
				errorString);
    	Picam_DestroyString(errorString);
    	status = asynError;
    }

    return (asynStatus) status;
}

/**
 * Callback method for camera discovery.  This method calls the
 * piHandleCameraDiscovery method of the camera instance.
 */
PicamError PIL_CALL ADPICam::piCameraDiscovered(const PicamCameraID *id,
        PicamHandle device, PicamDiscoveryAction action) {
    ADPICam_Instance->piHandleCameraDiscovery(id, device, action);

    return PicamError_None;
}

/**
 * Set all PICAM parameter exists parameters to false
 */
asynStatus ADPICam::piClearParameterExists() {
    int status = asynSuccess;

    for (std::unordered_map<PicamParameter, int>::value_type iParam :
            parameterExistsMap) {
        status |= setIntegerParam(iParam.second, 0);
    }
    return (asynStatus) status;
}

/**
 * Set all PICAM parameter relevance parameters to false
 */
asynStatus ADPICam::piClearParameterRelevance() {
    int status = asynSuccess;

    for (std::unordered_map<PicamParameter, int>::value_type iParam :
            parameterRelevantMap) {
        status |= setIntegerParam(iParam.second, 0);
    }
    return (asynStatus) status;
}

/**
 * Create and Index parameters associated (exists and relevant) with PI
 * parameter that is mapped to an existing AD parameter (from ADDriver
 * base class)
 */
asynStatus ADPICam::piCreateAndIndexADParam(const char * name,
        int adIndex, int &existsIndex,
        int &relevantIndex, PicamParameter picamParameter){
    int status = asynSuccess;

    parameterValueMap.emplace(picamParameter, adIndex);
    picamParameterMap.emplace(adIndex, picamParameter);
    status |= piCreateAndIndexPIAwarenessParam(name, existsIndex,
            relevantIndex, picamParameter);
    return (asynStatus)status;
}

/**
 * Create and Index parameters associated with awareness (exists and relevant)
 */
asynStatus ADPICam::piCreateAndIndexPIAwarenessParam(const char * name,
        int &existsIndex, int &relevantIndex,
        PicamParameter picamParameter){
    int status = asynSuccess;

    char existsName[256];
    char relevantName[256];

    strcpy(existsName, name);
    strcat(existsName, "_EX");
    strcpy(relevantName, name);
    strcat(relevantName, "_PR");
    status |= ADDriver::createParam(existsName, asynParamInt32, &existsIndex);
    status |= ADDriver::createParam(relevantName, asynParamInt32, &relevantIndex);
    parameterExistsMap.emplace(picamParameter, existsIndex);
    parameterRelevantMap.emplace(picamParameter, relevantIndex);

    return (asynStatus)status;
}

/**
 * Create and Index parameters associated (exists and relevant) with PI
 * parameter that is mapped to an parameter defined by this class (ADPICam)
 */
asynStatus ADPICam::piCreateAndIndexPIParam(const char * name, asynParamType type,
        int &index, int &existsIndex, int &relevantIndex,
        PicamParameter picamParameter){
    int status = asynSuccess;
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s,%s creating parameter %s type %d\n",
            driverName,
            __func__,
            name,
            type);
    status |= ADDriver::createParam(name, type, &index);
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s,%s creating parameter %s type %d index %d PICAM param %d\n",
            driverName,
            __func__,
            name,
            type,
            index,
            picamParameter);
    parameterValueMap.emplace(picamParameter, index);
    picamParameterMap.emplace(index, picamParameter);
    status |= piCreateAndIndexPIAwarenessParam(name, existsIndex,
            relevantIndex, picamParameter);

    return (asynStatus)status;
}

/**
 * Create and Index parameters associated (exists and relevant) with PI
 * parameter that is mapped to an parameter defined by this class (ADPICam)
 */
asynStatus ADPICam::piCreateAndIndexPIModulationsParam(const char * name,
        int &existsIndex, int &relevantIndex,
        PicamParameter picamParameter){
    int status = asynSuccess;
    status |= piCreateAndIndexPIAwarenessParam(name, existsIndex,
            relevantIndex, picamParameter);

    return (asynStatus)status;
}

/**
 * Create and Index parameters associated (exists and relevant) with PI
 * parameter that is mapped to an parameter defined by this class (ADPICam)
 */
asynStatus ADPICam::piCreateAndIndexPIPulseParam(const char * name,
        int &existsIndex, int &relevantIndex,
        PicamParameter picamParameter){
    int status = asynSuccess;

    status |= piCreateAndIndexPIAwarenessParam(name, existsIndex,
            relevantIndex, picamParameter);

    return (asynStatus)status;
}

/**
 * Create and Index parameters associated (exists and relevant) with PI
 * parameter that is mapped to an parameter defined by this class (ADPICam)
 */
asynStatus ADPICam::piCreateAndIndexPIRoisParam(const char * name,
        int &existsIndex, int &relevantIndex,
        PicamParameter picamParameter){
    int status = asynSuccess;

    status |= piCreateAndIndexPIAwarenessParam(name, existsIndex,
            relevantIndex, picamParameter);

    return (asynStatus)status;
}

/**
 *
 */
asynStatus ADPICam::piLoadAvailableCameraIDs() {
    const char *functionName = "piLoadAvailableCameraIDs";
    int status = asynSuccess;
    PicamError error = PicamError_None;
    const char *errorString;

    if (availableCamerasCount != 0) {
        error = Picam_DestroyCameraIDs(availableCameraIDs);
        availableCamerasCount = 0;
        if (error != PicamError_None) {
            Picam_GetEnumerationString(PicamEnumeratedType_Error, (piint) error,
                    &errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: Picam_DestroyCameraIDs error %s", driverName,
                    functionName, errorString);
            Picam_DestroyString(errorString);
            return asynError;
        }
    }
    error = Picam_GetAvailableCameraIDs(&availableCameraIDs,
            &availableCamerasCount);
    if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error, (piint) error,
                &errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s%s: Picam_GetAvailableCameraIDs error %s", driverName,
                functionName, errorString);
        Picam_DestroyString(errorString);
        return asynError;
    }
    return (asynStatus) status;
}

asynStatus ADPICam::piLoadUnavailableCameraIDs() {
    PicamError error;
    const char *errorString;
    const char *functionName = "piLoadUnavailableCameraIDs";
    int status = asynSuccess;

    if (unavailableCamerasCount != 0) {
        error = Picam_DestroyCameraIDs(unavailableCameraIDs);
        unavailableCamerasCount = 0;
        if (error != PicamError_None) {
            Picam_GetEnumerationString(PicamEnumeratedType_Error, (piint) error,
                    &errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: Picam_DestroyCameraIDs error %s", driverName,
                    functionName, errorString);
            Picam_DestroyString(errorString);
            return asynError;
        }
    }
    error = Picam_GetUnavailableCameraIDs(&unavailableCameraIDs,
            &unavailableCamerasCount);
    if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error, (piint) error,
                &errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s%s: Picam_GetUnavailableCameraIDs error %s", driverName,
                functionName, errorString);
        Picam_DestroyString(errorString);
        return asynError;
    }
    return (asynStatus) status;
}

/**
 * Given a PICAM library Parameter, return the associated areaDetector Driver
 * Parameter.
 */
int ADPICam::piLookupDriverParameter(PicamParameter parameter) {
    //const char *functionName = "piLookupDriverParameter";
    int driverParameter = -1;
    const char *paramString;

    try {
        driverParameter = parameterValueMap.at(parameter);
    }
    catch (std::out_of_range e) {
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
                &paramString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "---- Can't find parameter %s\n", paramString);
        Picam_DestroyString(paramString);

    }
    return driverParameter;

}

/**
 * Provide a translation between parameters for this camera (& those
 * inherited by ADDriver as necessary) and Picam's parameters.
 */
PicamError ADPICam::piLookupPICamParameter(int driverParameter,
        PicamParameter &parameter){
    const char *functionName = "piLookupPICamParameter";

    try {
        parameter = picamParameterMap.at(driverParameter);
    }
    catch (std::out_of_range e) {
        return PicamError_ParameterDoesNotExist;
    }
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s: driverParameter: %d, picamParam: %d\n",
            driverName,
            functionName,
            driverParameter,
            parameter);
    return PicamError_None;
}

asynStatus ADPICam::piGenerateListValuesFromCollection(
        asynUser *pasynUser, char *strings[],
        int values[], int severities[], size_t *nIn,
        int driverParam, PicamParameter picamParam){
    int status = asynSuccess;
    const PicamCollectionConstraint *constraints;
    const char *errorString;
    const char *paramConstraintString;
    const char *parameterName;
    const char *NAString = "N.A. 0";
    pibln paramExists;
    pibln isRelevant;
    PicamError error;

    if (currentCameraHandle != NULL) {
        Picam_DoesParameterExist(currentCameraHandle,
                picamParam,
                &paramExists);
        if (paramExists){
            error = Picam_IsParameterRelevant(currentCameraHandle,
                    picamParam,
                    &isRelevant);
            if (error != PicamError_None){
                Picam_GetEnumerationString(PicamEnumeratedType_Error,
                        error,
                        &errorString);
                PicamCameraID camID;
                Picam_GetCameraID(currentCameraHandle, &camID);
                const char *cameraModel;
                Picam_GetEnumerationString(PicamEnumeratedType_Model,
                        camID.model,
                        &cameraModel);
                asynPrint(pasynUser, ASYN_TRACE_ERROR,
                        "%s:%s Trouble getting relevance of parameter"
                        " associated with driver.  driverParam:%d, "
                        "picamParam:%d for camera %s, %s\n",
                        driverName,
                        __func__,
                        driverParam,
                        picamParam,
                        cameraModel,
                        errorString);
                Picam_DestroyString(cameraModel);
                Picam_DestroyString(errorString);
                return asynError;
            }
            PicamConstraintType constraintType;
            error = Picam_GetParameterConstraintType(currentCameraHandle,
                    picamParam,
                    &constraintType);
            if (error != PicamError_None) {
                Picam_GetEnumerationString(PicamEnumeratedType_Error,
                        error,
                        &errorString);
                Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                        picamParam,
                        &parameterName);
                asynPrint(pasynUser, ASYN_TRACE_ERROR,
                        "%s:%s Could not determine constraint type for "
                        "parameter %s. %s\n",
                        driverName,
                        __func__,
                        parameterName,
                        errorString);
                Picam_DestroyString(parameterName);
                Picam_DestroyString(errorString);
                return asynError;
            }
            if (isRelevant  &&
                    (constraintType == PicamConstraintType_Collection)){
                error = Picam_GetParameterCollectionConstraint(
                        currentCameraHandle,
                        picamParam,
                        PicamConstraintCategory_Capable, &constraints);
                if (error != PicamError_None){
                    Picam_GetEnumerationString(PicamEnumeratedType_Error,
                            error,
                            &errorString);
                    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                            "%s:%s Trouble getting parameter assocoated "
                            "with driver param %d, picam param:%d: %s\n",
                            driverName,
                            __func__,
                            driverParam,
                            picamParam,
                            errorString);
                    Picam_DestroyString(errorString);
                    return asynError;
                }
                if ( constraints->values_count == 0){
                    strings[0] = epicsStrDup(NAString);
                    values[0] = 0;
                    severities[0] = 0;
                    (*nIn) = 1;
                    return asynSuccess;
                }
                for (int ii = 0; ii < constraints->values_count; ii++) {
                    PicamEnumeratedType picamParameterET;
                    Picam_GetParameterEnumeratedType(currentCameraHandle,
                            picamParam,
                            &picamParameterET);
                    PicamValueType valType;
                    Picam_GetParameterValueType(currentCameraHandle,
                            picamParam, &valType);
                    if (strings[*nIn])
                        free(strings[*nIn]);
                    switch (valType)
                    {
                    case PicamValueType_Enumeration:
                        Picam_GetEnumerationString(
                            picamParameterET,
                            (int)constraints->values_array[ii],
                            &paramConstraintString);
                        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                                "%s:%s ---%s\n",
                                driverName,
                                __func__,
                                paramConstraintString);
                        asynPrint(pasynUser, ASYN_TRACE_FLOW,
                                "%s:%s constraint[%d] = %s\n",
                                driverName,
                                __func__,
                                ii,
                                paramConstraintString);

                        strings[*nIn] = epicsStrDup(paramConstraintString);
                        values[*nIn] = (int)constraints->values_array[ii];
                        severities[*nIn] = 0;
                        (*nIn)++;
                        Picam_DestroyString(paramConstraintString);
                        break;
                    case PicamValueType_FloatingPoint:
                        char floatString[12];
                        sprintf(floatString, "%f",
                                constraints->values_array[ii]);
                        strings[*nIn] = epicsStrDup(floatString);
                        values[*nIn] = ii;
                        severities[*nIn] = 0;
                        (*nIn)++;
                        break;
                    case PicamValueType_Integer:
                        char intString[12];
                        sprintf(intString, "%d",
                                (int)constraints->values_array[ii]);
                        strings[*nIn] = epicsStrDup(intString);
                        values[*nIn] = (int)constraints->values_array[ii];
                        severities[*nIn] = 0;
                        (*nIn)++;
                        break;
                    case PicamValueType_LargeInteger:
                        char largeIntString[12];
                        sprintf(largeIntString, "%ld",
                                (pi64s)constraints->values_array[ii]);
                        strings[*nIn] = epicsStrDup(largeIntString);
                        values[*nIn] = (int)constraints->values_array[ii];
                        severities[*nIn] = 0;
                        (*nIn)++;
                        break;
                    case PicamValueType_Boolean:
                        strings[*nIn] = epicsStrDup(
                                constraints->values_array[ii] ? "No":"Yes");
                        values[*nIn] = (int)constraints->values_array[ii];
                        severities[*nIn] = 0;
                        (*nIn)++;
                        break;
                    default:
                        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                                "%s:%s Unhandled CollectionType for "
                                "driverParam %d, picamParam:%d\n",
                                driverName,
                                __func__,
                                driverParam,
                                picamParam);
                        return asynError;
                    }
                }
            }
            else {
                Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                        picamParam,
                        &parameterName);
                asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                        "%s:%s Found not relevant & constraintType_Collection"
                        " for this detector %s\n",
                        driverName,
                        __func__,
                        parameterName);
                Picam_DestroyString(parameterName);
                strings[0] = epicsStrDup(NAString);
                values[0] = 0;
                severities[0] = 0;
                (*nIn) = 1;
            }
        }
    }
    else {
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                picamParam,
                &parameterName);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s Parameter Does Not Exist for this detector %s\n",
                driverName,
                __func__,
                parameterName);
        Picam_DestroyString(parameterName);
        strings[0] = epicsStrDup(NAString);
        values[0] = 0;
        severities[0] = 0;
        (*nIn) = 1;
    }
    return (asynStatus)status;
}

/**
 * Handler method for AcquisitionUpdated events.  Grab information
 * about acquired data, as necessary, and send a signal to a thread to
 * grab the data & process into NDArray.
 */
asynStatus ADPICam::piHandleAcquisitionUpdated(
        PicamHandle device,
        const PicamAvailableData *available,
        const PicamAcquisitionStatus *acqStatus)
{
    //const char * functionName = "piHandleAcquisitionUpdated";
    //int status = asynSuccess;

    dataLock.lock();
    acqStatusRunning = acqStatus->running;
    acqStatusErrors = acqStatus->errors;
    acqStatusReadoutRate = acqStatus->readout_rate;
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s available %p\n",
            driverName,
            __func__,
            available);
//    if ( (acqStatusErrors == PicamAcquisitionErrorsMask_None) &&
//    		acqStatusRunning){
    if (available && available->readout_count){
        acqAvailableInitialReadout = available->initial_readout;
        acqAvailableReadoutCount = available->readout_count;
        epicsEventSignal(piHandleNewImageEvent);
    }
    else {
        acqAvailableInitialReadout = NULL;
        acqAvailableReadoutCount = 0;
    }
    dataLock.unlock();
    epicsThreadSleep(0.000002);  // Twice the wait in piHandleNewImages

    return asynSuccess;
}

/**
 * Handler method for camera discovery events.  When new cameras become
 * available, or unavailable, move them on and off the lists as appropriate.
 */
asynStatus ADPICam::piHandleCameraDiscovery(const PicamCameraID *id,
        PicamHandle device, PicamDiscoveryAction action) {
    const char *functionName = "piHandleCameraDiscovery";
    //PicamError error = PicamError_None;
    int status = asynSuccess;
    const char* modelString;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Enter\n",
            driverName,
            functionName);

    piLoadAvailableCameraIDs();
    piUpdateAvailableCamerasList();
    switch (action) {
    case PicamDiscoveryAction_Found:
        Picam_GetEnumerationString(PicamEnumeratedType_Model, id->model,
                &modelString);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "Camera Found: %s\n",
                modelString);
        Picam_DestroyString(modelString);
        if (device != NULL) {
            PicamHandle discoveredModel;
            PicamAdvanced_GetCameraModel(device, &discoveredModel);
            printf(" discovered %p, current, %p\n", discoveredModel,
                    currentCameraHandle);
            if (discoveredModel == currentCameraHandle) {
                piSetSelectedCamera(pasynUserSelf, selectedCameraIndex);
            }

        }
        break;
    case PicamDiscoveryAction_Lost:
        Picam_GetEnumerationString(PicamEnumeratedType_Model, id->model,
                &modelString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "Camera Lost: %s\n",
                modelString);
        Picam_DestroyString(modelString);

        if (device != NULL) {
            PicamHandle discoveredModel;
            PicamAdvanced_GetCameraModel(device, &discoveredModel);
            printf(" discovered %p, current, %p", discoveredModel,
                    currentCameraHandle);
            if (discoveredModel == currentCameraHandle) {
                setStringParam(PICAM_CameraInterface, notAvailable);
                setStringParam(PICAM_SensorName, notAvailable);
                setStringParam(PICAM_SerialNumber, notAvailable);
                setStringParam(PICAM_FirmwareRevision, notAvailable);
                callParamCallbacks();
            }
        }
        break;
    default:
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "Unexpected discovery action%d", action);
    }
    if (device == NULL) {
        Picam_GetEnumerationString(PicamEnumeratedType_Model, id->model,
                &modelString);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "No device found with camera\n");
        Picam_DestroyString(modelString);
    } else {
        Picam_GetEnumerationString(PicamEnumeratedType_Model, id->model,
                &modelString);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "Device found with camera\n");

        Picam_DestroyString(modelString);

    }

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Exit\n",
            driverName,
            functionName);

    return (asynStatus) status;
}

/**
 * Handler method called by piParameterFloatingPointValueChanged callback method
 * Makes necessary since the parameter has changed.  Primarily will update the
 * Readback value for many parameters.
 */
asynStatus ADPICam::piHandleParameterFloatingPointValueChanged(
        PicamHandle camera, PicamParameter parameter, piflt value) {
    const char *functionName = "piHandleParameterFloatingPointValueChanged";
    PicamError error = PicamError_None;
    int status = asynSuccess;
    const pichar *parameterString;
    const pichar *errorString;
    int driverParameter;

    driverParameter = piLookupDriverParameter(parameter);
    //Handle the cases where simple translation between PICAM and areaDetector
    //is possible
    if (driverParameter >= 0) {
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
                &parameterString);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s Setting PICAM parameter %s to driverParameter %d, "
        		"value %f\n",
                driverName, functionName, parameterString, driverParameter,
                value);
        PicamConstraintType paramCT;
        error = Picam_GetParameterConstraintType(currentCameraHandle, parameter,
                &paramCT);
        if (error != PicamError_None) {
            Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                    &errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Trouble determining constraint type for parameter %s"
                    "  %s",
                    driverName,
                    __func__,
                    parameterString,
                    errorString);
            Picam_DestroyString(errorString);
        }
        Picam_DestroyString(parameterString);
        switch (paramCT) {
        case PicamConstraintType_Collection:
            const PicamCollectionConstraint *constraint;
            error = Picam_GetParameterCollectionConstraint(
                    currentCameraHandle,
                    parameter,
                    PicamConstraintCategory_Capable,
                    &constraint);
            if (error != PicamError_None) {
                Picam_GetEnumerationString(PicamEnumeratedType_Error,
                        error,
                        &errorString);
                asynPrint (pasynUserSelf, ASYN_TRACE_ERROR,
                        "%s:%s ErrorGetting constraint for "
                        "parameter %s. %s\n",
                        driverName,
                        __func__,
                        parameterString,
                        errorString);
                Picam_DestroyString(errorString);
                return asynError;
            }
            for (int iParam = 0; iParam < constraint->values_count;
                    iParam++) {
                if (constraint->values_array[iParam] == value) {
                    status |= setIntegerParam(driverParameter, iParam);
                }
            }
            break;
        default:

            status |= setDoubleParam(driverParameter, value);
            break;
        }
        } else {
            // Notify that handling a parameter is about to fall on the floor
            // unhandled
            Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
                    &parameterString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Parameter %s floating point value Changed to %f.  "
                            "This change is not handled.\n", driverName,
                    functionName, parameterString, value);
            Picam_DestroyString(parameterString);
    }
    callParamCallbacks();
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Exit\n", driverName,
            functionName);

    return (asynStatus) status;
}

/**
 * Handler method called by piParameterIntegerValueChanged callback method
 * Makes necessary since the parameter has changed.  Primarily will update the
 * Readback value for many parameters.
 */
asynStatus ADPICam::piHandleParameterIntegerValueChanged(PicamHandle camera,
        PicamParameter parameter, piint value) {
    const char *functionName = "piHandleParameterIntegerValueChanged";
    PicamError error = PicamError_None;
    int status = asynSuccess;
    const char *parameterString;
    const char *errorString;
    int driverParameter;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Enter\n",
            driverName,
            functionName);

    driverParameter = piLookupDriverParameter(parameter);
    //Handle parameters that are as easy as translating between PICAM and
    //areaDetector
    if (driverParameter >= 0) {
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
                &parameterString);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s Setting PICAM parameter %s to driverParameter %d, "
                "value %d\n",
                driverName, functionName, parameterString, driverParameter,
                value);
        Picam_DestroyString(parameterString);
        PicamConstraintType paramCT;
        error = Picam_GetParameterConstraintType(currentCameraHandle, parameter,
                &paramCT);
        if (error != PicamError_None) {
            Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                    &errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Trouble determining constraint type for parameter %s"
                    "  %s",
                    driverName,
                    __func__,
                    parameterString,
                    errorString);
            Picam_DestroyString(errorString);
        }
        Picam_DestroyString(parameterString);
        switch (paramCT) {
        case PicamConstraintType_Collection:
            const PicamCollectionConstraint *constraint;
            error = Picam_GetParameterCollectionConstraint(
                    currentCameraHandle,
                    parameter,
                    PicamConstraintCategory_Capable,
                    &constraint);
            if (error != PicamError_None) {
                Picam_GetEnumerationString(PicamEnumeratedType_Error,
                        error,
                        &errorString);
                asynPrint (pasynUserSelf, ASYN_TRACE_ERROR,
                        "%s:%s ErrorGetting constraint for "
                        "parameter %s. %s\n",
                        driverName,
                        __func__,
                        parameterString,
                        errorString);
                Picam_DestroyString(errorString);
            }
            for (int iParam = 0; iParam < constraint->values_count;
                    iParam++) {
                if (constraint->values_array[iParam] == value) {
                    setIntegerParam(driverParameter, iParam);
                }
            }
            break;
        default:
            status = setIntegerParam(driverParameter, value);
        }
    } else {
        // Pass along to method that lets you know that a parameter change is
        // about to fall on the ground unhandled.
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
                &parameterString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Parameter %s integer value Changed %d."
                        " This change was unhandled.\n", driverName,
                functionName, parameterString, (int )value);
        Picam_DestroyString(parameterString);
    }
    callParamCallbacks();

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Exit\n",
            driverName,
            functionName);

    return (asynStatus) status;
}

/**
 * Handler method called by piParameterLargeIntegerValueChanged callback method
 * Makes necessary since the parameter has changed.  Primarily will update the
 * Readback value for many parameters.
 */
asynStatus ADPICam::piHandleParameterLargeIntegerValueChanged(
        PicamHandle camera,
        PicamParameter parameter,
        pi64s value) {
    const char *functionName = "piHandleParameterLargeIntegerValueChanged";
    //PicamError error = PicamError_None;
    int status = asynSuccess;
    const char *parameterString;
    int driverParameter;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Enter\n",
            driverName,
            functionName);
    //Handle parameters that are a simple lookup between areaDetector and PICAM
    driverParameter = piLookupDriverParameter(parameter);
    if (driverParameter >= 0) {
        long lValue = (long) value;
        setIntegerParam(driverParameter, lValue);
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
                &parameterString);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s Setting PICAM parameter %s to driverParameter %d, "
                "value %ld long value %ld\n",
                driverName, functionName, parameterString, driverParameter,
                value, lValue);
        Picam_DestroyString(parameterString);
    // Notify that handling a parameter is about to fall on the floor unhandled
    } else {
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
                &parameterString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        		"%s:%s Parameter %s large integer value Changed %ld.\n"
        		"This change was unhandled\n",
                driverName,
				functionName,
				parameterString,
				value);
        Picam_DestroyString(parameterString);
    }

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Exit\n", driverName,
            functionName);
    return (asynStatus) status;
}

/**
 * Handle case when a PicamModulations value has changed.  Called by
 * piParameterModulationsValueChanged.
 */
asynStatus ADPICam::piHandleParameterModulationsValueChanged(PicamHandle camera,
        PicamParameter parameter, const PicamModulations *value) {
    const char *functionName = "piHandleParameterModulationsValueChanged";
    //PicamError error = PicamError_None;
    int status = asynSuccess;
    const char *parameterString;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Enter\n", driverName,
            functionName);
    Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
            &parameterString);
    printf("parameter %s Modulations value Changed to %p", parameterString,
            value);
    Picam_DestroyString(parameterString);

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Exit\n", driverName,
            functionName);

    return (asynStatus) status;
}

/**
 * Handle case when a PicamPulse value has changed.  Called by
 * piParameterPulseValueChanged.
 */
asynStatus ADPICam::piHandleParameterPulseValueChanged(PicamHandle camera,
        PicamParameter parameter, const PicamPulse *value) {
    const char *functionName = "piHandleParameterPulseValueChanged";
    //PicamError error = PicamError_None;
    int status = asynSuccess;
    const char *parameterString;
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Enter\n", driverName,
            functionName);
    Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
            &parameterString);
    printf("parameter %s Pulse value Changed to %p\n", parameterString, value);
    Picam_DestroyString(parameterString);

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Exit\n", driverName,
            functionName);

    return (asynStatus) status;
}

/**
 * Handler method called by piParameterRelevanceChanged callback method
 * Sets the relevence of a parameter based on changes in parameters.
 */
asynStatus ADPICam::piHandleParameterRelevanceChanged(PicamHandle camera,
        PicamParameter parameter, pibln relevant) {
    const char *functionName = "piHandleParameterRelevanceChanged";
    //PicamError error = PicamError_None;
    int status = asynSuccess;
    PicamConstraintType constraintType;
    PicamValueType valueType;
    int driverParameter;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Enter",
            driverName,
            functionName);

    status = piSetParameterRelevance(pasynUserSelf, parameter, (int) relevant);
    if (relevant != 0) {
    	Picam_GetParameterConstraintType(currentCameraHandle,
    			parameter,
				&constraintType);
    	Picam_GetParameterValueType(currentCameraHandle, parameter, &valueType);
    	driverParameter = piLookupDriverParameter(parameter);
    	if ((driverParameter > 0) &&
    			((constraintType == PicamConstraintType_Collection) ||
    					(valueType == PicamValueType_Enumeration))) {
    		piUpdateParameterListValues(parameter, driverParameter);
    	}
    }
    return (asynStatus) status;
}

/**
 * Handle the case that an ROI value has changed.
 */
asynStatus ADPICam::piHandleParameterRoisValueChanged(PicamHandle camera,
        PicamParameter parameter, const PicamRois *value) {
    const char *functionName = "piHandleParameterRoisValueChanged";
    //PicamError error = PicamError_None;
    int status = asynSuccess;
    const char *parameterString;
    Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
            &parameterString);
    printf("parameter %s Rois value Changed\n", parameterString);
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "\n----- minX = %d\n----- minY = %d\n"
                    "----- sizeX = %d\n----- sizeY = %d\n"
                    "----- binX = %d\n----- binY = %d\n", value->roi_array[0].x,
            value->roi_array[0].y, value->roi_array[0].width,
            value->roi_array[0].height, value->roi_array[0].x_binning,
            value->roi_array[0].y_binning);
    setIntegerParam(NDArraySizeX, value->roi_array[0].width/
    		value->roi_array[0].x_binning);
    setIntegerParam(NDArraySizeY, value->roi_array[0].height/
    		value->roi_array[0].y_binning);
    Picam_DestroyString(parameterString);

    callParamCallbacks();
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Exit\n", driverName,
            functionName);



    return (asynStatus) status;
}

/**
 * Callback to Handle when a FloatingPoint value changes.  Hand off to
 * method piHandleParameterFloatingPointValue of the stored class instance
 */
PicamError PIL_CALL ADPICam::piParameterFloatingPointValueChanged(
        PicamHandle camera, PicamParameter parameter, piflt value) {
    int status = asynSuccess;
    PicamError error = PicamError_None;
    //const char *functionName = "piParameterFloatingPointValueChanged";

    status = ADPICam_Instance->piHandleParameterFloatingPointValueChanged(
            camera, parameter, value);

    if (status != asynSuccess) {
    	error = PicamError_UnexpectedError;
    }

    return error;
}

/**
 * Callback method to handle when an Integer Value Changes.  Hand off to the
 * method piHandleParameterIntergerValueChanged of the stored class instance.
 */
PicamError PIL_CALL ADPICam::piParameterIntegerValueChanged(PicamHandle camera,
        PicamParameter parameter, piint value) {
    int status = asynSuccess;
    PicamError error = PicamError_None;
    //const char *functionName = "piParameterIntegerValueChanged";

    status = ADPICam_Instance->piHandleParameterIntegerValueChanged(camera,
            parameter, value);

    if (status != asynSuccess) {
    	error = PicamError_UnexpectedError;
    }

    return error;
}

/**
 * Callback to Handle when a LargeInteger value changes.  Hand of to the method
 * piHandleParameterLargeIntergerValueChanged of the stored class instance.
 */
PicamError PIL_CALL ADPICam::piParameterLargeIntegerValueChanged(
        PicamHandle camera, PicamParameter parameter, pi64s value) {
    int status = asynSuccess;
    PicamError error = PicamError_None;
    //const char *functionName = "piParameterLargeIntegerValueChanged";

    status = ADPICam_Instance->piHandleParameterLargeIntegerValueChanged(camera,
            parameter, value);

    if (status != asynSuccess) {
    	error = PicamError_UnexpectedError;
    }

    return error;
}

/**
 * Callback to Handle when a PicamModulations value changes.  Hand off to the
 * method picamHandleModulationValueChanged of the stored class instance.
 */
PicamError PIL_CALL ADPICam::piParameterModulationsValueChanged(
        PicamHandle camera, PicamParameter parameter,
        const PicamModulations *value) {
    int status = asynSuccess;
    PicamError error = PicamError_None;
    //const char *functionName = "piParameterModulationsValueChanged";

    status = ADPICam_Instance->piHandleParameterModulationsValueChanged(camera,
            parameter, value);

    if (status != asynSuccess) {
    	error = PicamError_UnexpectedError;
    }

    return error;
}

/**
 * Callback to Handle when a PicamPulse value changes.  Calls method
 * piHandleParameterPulseValueChanged of the stored class instance.
 */
PicamError PIL_CALL ADPICam::piParameterPulseValueChanged(PicamHandle camera,
        PicamParameter parameter, const PicamPulse *value) {
    int status = asynSuccess;
    PicamError error = PicamError_None;
    //const char *functionName = "piParameterPulseValueChanged";

    status = ADPICam_Instance->piHandleParameterPulseValueChanged(camera,
            parameter, value);

    if (status != asynSuccess) {
    	error = PicamError_UnexpectedError;
    }

    return error;
}

/**
 * Callback event to catch when a parameter's relevance has changed.  Calls
 * method piHandleParameterRelevanceChanged of stored class instance.
 */
PicamError PIL_CALL ADPICam::piParameterRelevanceChanged(PicamHandle camera,
        PicamParameter parameter, pibln relevent) {
    int status = asynSuccess;
    PicamError error = PicamError_None;

    status = ADPICam_Instance->piHandleParameterRelevanceChanged(camera,
            parameter, relevent);

    if (status != asynSuccess) {
    	error = PicamError_UnexpectedError;
    }

    return error;
}

/**
 * Callback to Handle when a Roi value changes.  Calls method
 * piHandleParameterRoisValueChanged of the stored class instance.
 */
PicamError PIL_CALL ADPICam::piParameterRoisValueChanged(PicamHandle camera,
        PicamParameter parameter, const PicamRois *value) {
    int status = asynSuccess;
    PicamError error = PicamError_None;
    //const char *functionName = "piParameterRoisValueChanged";

    status = ADPICam_Instance->piHandleParameterRoisValueChanged(camera,
            parameter, value);

    if (status != asynSuccess) {
    	error = PicamError_UnexpectedError;
    }

    return error;
}

/**
 * Print the Rois constraint information.
 */
asynStatus ADPICam::piPrintRoisConstraints() {
    const char *functionName = "piPrintRoisConstraints";
    PicamError error = PicamError_None;
    const PicamRoisConstraint *roisConstraints;
    const char *errorString;
    int status = asynSuccess;

    error = Picam_GetParameterRoisConstraint(currentCameraHandle,
            PicamParameter_Rois, PicamConstraintCategory_Capable,
            &roisConstraints);
    if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                &errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Error retrieving rois constraints %s\n", driverName,
                functionName, errorString);
        Picam_DestroyString(errorString);
        return asynError;
    }

    return (asynStatus)status;
}

/**
 *Register callbacks for constraint changes.
 */
asynStatus ADPICam::piRegisterConstraintChangeWatch(PicamHandle cameraHandle) {
    int status = asynSuccess;
    piint parameterCount;
    const PicamParameter *parameterList;
    PicamError error;
    const char *errorString;
    const char *paramString;

    error = Picam_GetParameters(currentCameraHandle, &parameterList,
            &parameterCount);
    if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error,
        		error,
				&errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        		"%s:%s Error: Trouble getting list of parameters for current "
        		"camera.  %s\n",
				driverName,
				__func__,
				errorString);
        Picam_DestroyString(errorString);
        return asynError;
    }
    for (int ii = 0; ii < parameterCount; ii++) {
        //TODO need to change to constraint change watches.  These depend on
    	// parameter types.
    	error = PicamAdvanced_RegisterForIsRelevantChanged(cameraHandle,
                parameterList[ii], piParameterRelevanceChanged);
        if (error != PicamError_None) {
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
					&errorString);
        	Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
        			parameterList[ii],
					&paramString);

        	asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        			"%s:%s Trouble registering ConstraintChange for "
        			"parameter %s. %s\n",
					driverName,
					__func__,
					paramString,
					errorString);
        	Picam_DestroyString(paramString);
        	Picam_DestroyString(errorString);
        }
    }
    return (asynStatus) status;
}

/**
 * Register to watch to changes in parameter relevance
 */
asynStatus ADPICam::piRegisterRelevantWatch(PicamHandle cameraHandle) {
    int status = asynSuccess;
    piint parameterCount;
    const PicamParameter *parameterList;
    PicamError error;
    const char *errorString;
    const char *paramString;

    error = Picam_GetParameters(currentCameraHandle, &parameterList,
            &parameterCount);
    if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error,
        		error,
				&errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        		"%s:%s Error: Trouble getting list of parameters for current "
        		"camera.  %s\n",
				driverName,
				__func__,
				errorString);
        Picam_DestroyString(errorString);
        return asynError;
    }
    for (int ii = 0; ii < parameterCount; ii++) {
        error = PicamAdvanced_RegisterForIsRelevantChanged(cameraHandle,
                parameterList[ii], piParameterRelevanceChanged);
        if (error != PicamError_None) {
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
					&errorString);
        	Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
        			parameterList[ii],
					&paramString);

        	asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        			"%s:%s Trouble registering RelevantWatch for "
        			"parameter %s. %s\n",
					driverName,
					__func__,
					paramString,
					errorString);
        	Picam_DestroyString(paramString);
        	Picam_DestroyString(errorString);
        }
    }
    return (asynStatus) status;
}

/**
 * Register to watch for changes in a parameter's value
 */
asynStatus ADPICam::piRegisterValueChangeWatch(PicamHandle cameraHandle) {
    int status = asynSuccess;
    piint parameterCount;
    const PicamParameter *parameterList;
    PicamValueType valueType;
    PicamError error;
    const char *functionName = "piRegisterValueChangeWatch";
    pibln doesParamExist;
    const char *errorString;
    const char *paramString;

    error = Picam_GetParameters(currentCameraHandle, &parameterList,
            &parameterCount);
    if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error,
        		error,
				&errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        		"%s:%s Error: Trouble getting list of parameters for current "
        		"camera.  %s\n",
				driverName,
				__func__,
				errorString);
        Picam_DestroyString(errorString);
        return asynError;
    }
    for (int ii = 0; ii < parameterCount; ii++) {
    	Picam_DoesParameterExist(currentCameraHandle,
    			parameterList[ii],
				&doesParamExist);
    	if (doesParamExist) {
			error = Picam_GetParameterValueType(cameraHandle, parameterList[ii],
					&valueType);
			if (error != PicamError_None) {
		        Picam_GetEnumerationString(PicamEnumeratedType_Error,
		        		error,
						&errorString);
	        	Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
	        			parameterList[ii],
						&paramString);
				asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
						"%s:%s Trouble getting parameter value type for "
						"parameter %s. %s\n",
						driverName,
						functionName,
						paramString,
						errorString);
		        Picam_DestroyString(paramString);
		        Picam_DestroyString(errorString);
				return asynError;
			}
			switch (valueType) {
			case PicamValueType_Integer:
			case PicamValueType_Boolean:
			case PicamValueType_Enumeration:
				error = PicamAdvanced_RegisterForIntegerValueChanged(cameraHandle,
						parameterList[ii], piParameterIntegerValueChanged);
				break;
			case PicamValueType_LargeInteger:
				error = PicamAdvanced_RegisterForLargeIntegerValueChanged(
						cameraHandle, parameterList[ii],
						piParameterLargeIntegerValueChanged);
				break;
			case PicamValueType_FloatingPoint:
				error = PicamAdvanced_RegisterForFloatingPointValueChanged(
						cameraHandle, parameterList[ii],
						piParameterFloatingPointValueChanged);
				break;
			case PicamValueType_Rois:
				printf("Registering ROIS value changed\n");
				error = PicamAdvanced_RegisterForRoisValueChanged(cameraHandle,
						parameterList[ii], piParameterRoisValueChanged);
				break;
			case PicamValueType_Pulse:
				error = PicamAdvanced_RegisterForPulseValueChanged(cameraHandle,
						parameterList[ii], piParameterPulseValueChanged);
				break;
			case PicamValueType_Modulations:
				error = PicamAdvanced_RegisterForModulationsValueChanged(
						cameraHandle, parameterList[ii],
						piParameterModulationsValueChanged);
				break;
			default: {
				asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
						"%s:%s Unexpected valueType %d", driverName, functionName,
						valueType);
				return asynError;
			}
				break;
			}
    	}
    }
    return (asynStatus) status;
}

/**
 * Set the value stored in a parameter existance PV
 */
asynStatus ADPICam::piSetParameterExists(asynUser *pasynUser,
        PicamParameter parameter, int exists) {
    int status = asynSuccess;
    int driverParameter = -1;
    //static const char *functionName = "piSetParameterExists";
    const pichar* string;

    try {
        driverParameter = parameterExistsMap.at(parameter);
    }
    catch (std::out_of_range e) {
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
                &string);
        asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s ---- Can't find parameter %s\n",
                driverName,
                __func__,
                string);
        Picam_DestroyString(string);
        return asynError;
    }
    setIntegerParam(driverParameter, exists);
    return (asynStatus) status;
}

/**
 * Set the value stored in a parameter relevance PV
 */
asynStatus ADPICam::piSetParameterRelevance(asynUser *pasynUser,
        PicamParameter parameter, int relevence) {
    int status = asynSuccess;
    int driverParameter = -1;
    //static const char *functionName = "piSetSelectedCamera";
    const pichar* string;

    try {
        driverParameter = parameterRelevantMap.at(parameter);
    }
    catch (std::out_of_range e) {
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter, parameter,
                &string);
        asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s ---- Can't find parameter %s\n",
                driverName,
                __func__,
                string);
        Picam_DestroyString(string);
        return asynError;
    }

    setIntegerParam(driverParameter, relevence);
    return (asynStatus) status;
}

/**
 * Change the parameter values based on those stored in the camera as the
 * selected detector changes.
 */
asynStatus ADPICam::piSetParameterValuesFromSelectedCamera() {
    int status = asynSuccess;
    PicamError error;
    const pichar *paramString;
    const pichar *errorString;
    piint parameterCount;
    const PicamParameter *parameterList;
    static const char *functionName = "piSetParameterValuesFromSelectedCamera";
    int driverParam = -1;
    PicamValueType paramType;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Enter\n", driverName,
            functionName);

    Picam_GetParameters(currentCameraHandle, &parameterList, &parameterCount);
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s %d parameters found\n",
            driverName, functionName, parameterCount);

    for (int ii = 0; ii < parameterCount; ii++) {
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                parameterList[ii], &paramString);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "---- Found %s\n",
                paramString);
        driverParam = piLookupDriverParameter(parameterList[ii]);
        pibln doesParamExist;
        Picam_DoesParameterExist(currentCameraHandle,
        		parameterList[ii],
				&doesParamExist);
        if (doesParamExist) {
			if (driverParam >= 0) {
				error = Picam_GetParameterValueType(currentCameraHandle,
						parameterList[ii], &paramType);
				if (error != PicamError_None) {
					Picam_GetEnumerationString(PicamEnumeratedType_Error,
							error,
							&errorString);
					asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
							"%s:%s Trouble getting parameter value type for "
							"parameter %s. %s",
							driverName,
							__func__,
							paramString,
							errorString);
					Picam_DestroyString(paramString);
					Picam_DestroyString(errorString);
					return asynError;
				}
				PicamConstraintType constraintType;
				Picam_GetParameterConstraintType(currentCameraHandle,
						parameterList[ii],
						&constraintType);
				if (constraintType == PicamConstraintType_Collection){
					const pichar *paramString;
					Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
							parameterList[ii],
							&paramString);
					asynPrint (pasynUserSelf, ASYN_TRACE_FLOW,
							"%s:%s Updating list for %s\n",
							driverName,
							functionName,
							paramString);
					Picam_DestroyString(paramString);
					piUpdateParameterListValues(parameterList[ii], driverParam);
				}
				switch (paramType) {
				case PicamValueType_Integer:
					piint intVal;
					error = Picam_GetParameterIntegerValue(currentCameraHandle,
							parameterList[ii], &intVal);
					if (error != PicamError_None) {
						Picam_GetEnumerationString(PicamEnumeratedType_Error,
								error,
								&errorString);
						asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
								"%s:%s Trouble getting Integer parameter %s\n",
								driverName,
								functionName,
								errorString);
						Picam_DestroyString(errorString);
						return asynError;
					}
                    PicamConstraintType paramCT;
                    error = Picam_GetParameterConstraintType(
                            currentCameraHandle,
                            parameterList[ii],
                            &paramCT);
                    if (error != PicamError_None) {
                        Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                                &errorString);
                        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                                "%s:%s Trouble determining constraint type "
                                "for parameter %s.  %s",
                                driverName,
                                __func__,
                                paramString,
                                errorString);
                        Picam_DestroyString(errorString);
                    }
                    switch (paramCT) {
                    case PicamConstraintType_Collection:
                        const PicamCollectionConstraint *constraint;
                        error = Picam_GetParameterCollectionConstraint(
                                currentCameraHandle,
                                parameterList[ii],
                                PicamConstraintCategory_Capable,
                                &constraint);
                        if (error != PicamError_None) {
                            Picam_GetEnumerationString(PicamEnumeratedType_Error,
                                    error,
                                    &errorString);
                            asynPrint (pasynUserSelf, ASYN_TRACE_ERROR,
                                    "%s:%s ErrorGetting constraint for "
                                    "parameter %s. %s\n",
                                    driverName,
                                    __func__,
                                    paramString,
                                    errorString);
                            Picam_DestroyString(errorString);
                        }
                        for (int iParam = 0; iParam < constraint->values_count;
                                iParam++) {
                            if ((int)constraint->values_array[ii] == intVal) {
                                setIntegerParam(driverParam, ii);
                            }
                        }

                        break;
                    default:
					setIntegerParam(driverParam, intVal);
                    }
					break;
				case PicamValueType_Enumeration:
					if (constraintType != PicamConstraintType_None) {
						error = Picam_GetParameterIntegerValue(
								currentCameraHandle,
								parameterList[ii],
								&intVal);
						if (error != PicamError_None) {
							Picam_GetEnumerationString(
									PicamEnumeratedType_Error,
									error,
									&errorString);
							asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
									"%s:%s Trouble getting Integer parameter "
									"%s\n",
									driverName,
									functionName,
									errorString);
							Picam_DestroyString(errorString);
							return asynError;
						}
						setIntegerParam(driverParam, intVal);
					}
					else {
						PicamEnumeratedType picamET;
						const char *enumString;
						int intValue;
						Picam_GetParameterEnumeratedType(currentCameraHandle,
								parameterList[ii],
								&picamET);
						Picam_GetParameterIntegerValue(currentCameraHandle,
												parameterList[ii], &intValue);
						Picam_GetEnumerationString(picamET,
								intValue,
								&enumString);
						setStringParam(driverParam, enumString);
						Picam_DestroyString(enumString);
					}
					break;
				case PicamValueType_LargeInteger:
					pi64s largeVal;
					int val;
					error = Picam_GetParameterLargeIntegerValue(
							currentCameraHandle,
							parameterList[ii],
							&largeVal);
					if (error != PicamError_None) {
						Picam_GetEnumerationString(PicamEnumeratedType_Error,
								error,
								&errorString);
						asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
								"%s:%s Trouble getting Large "
								"Integer parameter %s\n",
								driverName,
								functionName,
								errorString);
						Picam_DestroyString(errorString);
						return asynError;
					}
					val = (int)largeVal;
					setIntegerParam(driverParam, val);
					break;
				case PicamValueType_FloatingPoint:
					piflt fltVal;
					//const PicamCollectionConstraint *speedConstraint;
					error = Picam_GetParameterFloatingPointValue(
								currentCameraHandle, parameterList[ii], &fltVal);
                    if (error != PicamError_None) {
                        Picam_GetEnumerationString(PicamEnumeratedType_Error,
                                error,
                                &errorString);
                        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                                "%s:%s Trouble getting collection "
                                "constraint for parameter %s, %s\n",
                                driverName,
                                functionName,
                                paramString,
                                errorString);
                        Picam_DestroyString(errorString);
                        return asynError;
                    }
                    status |= piHandleParameterFloatingPointValueChanged(
                            currentCameraHandle,
                            parameterList[ii],
                            fltVal);
//                    error = Picam_GetParameterConstraintType(
//                            currentCameraHandle,
//                            parameterList[ii],
//                            &paramCT);
//                    if (error != PicamError_None) {
//                        Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
//                                &errorString);
//                        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
//                                "%s:%s Trouble determining constraint type "
//                                "for parameter %s.  %s",
//                                driverName,
//                                __func__,
//                                paramString,
//                                errorString);
//                        Picam_DestroyString(errorString);
//                    }
//                    switch (paramCT) {
//                    case PicamConstraintType_Collection:
//                        const PicamCollectionConstraint *constraint;
//                        error = Picam_GetParameterCollectionConstraint(
//                                currentCameraHandle,
//                                parameterList[ii],
//                                PicamConstraintCategory_Capable,
//                                &constraint);
//                        if (error != PicamError_None) {
//                            Picam_GetEnumerationString(PicamEnumeratedType_Error,
//                                    error,
//                                    &errorString);
//                            asynPrint (pasynUserSelf, ASYN_TRACE_ERROR,
//                                    "%s:%s ErrorGetting constraint for "
//                                    "parameter %s. %s\n",
//                                    driverName,
//                                    __func__,
//                                    paramString,
//                                    errorString);
//                            Picam_DestroyString(errorString);
//                        }
//                        for (int iParam = 0; iParam < constraint->values_count;
//                                iParam++) {
//                            if (constraint->values_array[iParam] == fltVal) {
//                                setIntegerParam(driverParam, iParam);
//                            }
//                        }
//                        break;
//                    default:
//                        status |= setDoubleParam(driverParam, fltVal);
//                        break;
//                    }
        default:
          break;
				}

			} else if (parameterList[ii] == PicamParameter_Rois) {
				const PicamRois *paramRois;
				const PicamRoisConstraint *roiConstraint;
				error = Picam_GetParameterRoisValue(currentCameraHandle,
						parameterList[ii], &paramRois);
				if (error != PicamError_None) {
					Picam_GetEnumerationString(PicamEnumeratedType_Error,
							error,
							&errorString);
					asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
							"%s:%s Trouble getting ROI value. %s",
							driverName,
							__func__,
							errorString);
					Picam_DestroyString(errorString);
					return asynError;
				}
				error = Picam_GetParameterRoisConstraint(currentCameraHandle,
						parameterList[ii], PicamConstraintCategory_Capable,
						&roiConstraint);
				if (error != PicamError_None) {
					Picam_GetEnumerationString(PicamEnumeratedType_Error,
							error,
							&errorString);
					asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
							"%s:%s Trouble getting ROI constraint. %s",
							driverName,
							__func__,
							errorString);
					Picam_DestroyString(errorString);
					return asynError;
				}
				asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "Rois %d, rules 0X%X\n",
						paramRois->roi_count,  roiConstraint->rules);

				if (paramRois->roi_count == 1) {
					setIntegerParam(ADBinX, paramRois->roi_array[0].x_binning);
					setIntegerParam(ADBinY, paramRois->roi_array[0].y_binning);
					setIntegerParam(ADMinX, paramRois->roi_array[0].x);
					setIntegerParam(ADMinY, paramRois->roi_array[0].y);
					setIntegerParam(ADSizeX, paramRois->roi_array[0].width);
					setIntegerParam(ADSizeY, paramRois->roi_array[0].height);
					setIntegerParam(NDArraySizeX, paramRois->roi_array[0].width);
					setIntegerParam(NDArraySizeY, paramRois->roi_array[0].height);
					if (roiConstraint->rules & PicamRoisConstraintRulesMask_HorizontalSymmetry) {
						setIntegerParam(PICAM_EnableROIMinXInput, 0);
					}
					else {
						setIntegerParam(PICAM_EnableROIMinXInput, 1);
					}
					if (roiConstraint->rules & PicamRoisConstraintRulesMask_VerticalSymmetry) {
						setIntegerParam(PICAM_EnableROIMinYInput, 0);
					}
					else {
						setIntegerParam(PICAM_EnableROIMinYInput, 1);
					}
				}
			}
        }
        Picam_DestroyString(paramString);
    }

    Picam_DestroyParameters(parameterList);
    callParamCallbacks();

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Exit\n", driverName,
            functionName);

    return (asynStatus) status;
}

/**
 * Set values for the ROI parameters.  PICAM holds these parameters in a single
 * object instead of as separate parameters.
 */
asynStatus ADPICam::piSetRois(int minX, int minY, int width, int height,
        int binX, int binY) {
    int status = asynSuccess;
    const PicamRois *rois;
    const PicamRoisConstraint *roisConstraints;
    const char *functionName = "piSetRois";
    int numXPixels, numYPixels;
    const pichar *errorString;

    PicamError error = PicamError_None;
    getIntegerParam(ADMaxSizeX, & numXPixels);
    getIntegerParam(ADMaxSizeY, & numYPixels);
    error = Picam_GetParameterRoisValue(currentCameraHandle,
            PicamParameter_Rois, &rois);
    if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                &errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Error retrieving rois %s\n", driverName, functionName,
                errorString);
        Picam_DestroyString(errorString);
        return asynError;
    }
    error = Picam_GetParameterRoisConstraint(currentCameraHandle,
            PicamParameter_Rois, PicamConstraintCategory_Required,
            &roisConstraints);
    printf ("ROIConstraints->rules 0x%X\n", roisConstraints->rules);
    if (error != PicamError_None) {
        Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                &errorString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "%s:%s Error retrieving rois constraints %s\n", driverName,
                functionName, errorString);
        Picam_DestroyString(errorString);
        return asynError;
    }
    if (rois->roi_count == 1) {
        PicamRoi *roi = &(rois->roi_array[0]);
        //bool allInRange = true;
        if (binX == 0) binX = 1;
        if (roisConstraints->rules & PicamRoisConstraintRulesMask_HorizontalSymmetry) {
        	if (width >= numXPixels/binX){
        		width = numXPixels/binX;
        	}
        	//make sure pixels in each quadrant are divisible by binnning
        	if ((((width/2)/binX) * binX) * 2 != width){
    			width = (((width/2)/binX) * binX)*2;
    		}
        	roi->x = ((numXPixels + 1)/2 ) - ((width/2) * binX) ;
        	roi->width = width * binX;
        	roi->x_binning = binX;
        	setIntegerParam(ADMinX, roi->x);
        	setIntegerParam(ADSizeX, width);
        	setIntegerParam(ADBinX, binX);
        }
        else {

        	if (minX < 1) {
                minX = 1;
            } else if (minX > numXPixels-binX) {
            	minX = numXPixels;
            }
            roi->x = minX;
            setIntegerParam(ADMinX, minX);
            if (width < 1){
                width = 1;
            } else if (width > (numXPixels - minX + 1)/binX) {
            	width = (numXPixels - minX + 1)/binX;
            	if (width < 1) {
            		width = 1;
            	}
            }
            roi->width = width * binX;
            roi->x_binning = binX;
            setIntegerParam(ADSizeX, width);
            setIntegerParam(ADBinX, binX);

        }
        if (binY == 0) binY = 1;
        if (roisConstraints->rules & PicamRoisConstraintRulesMask_VerticalSymmetry) {
        	if (height >= numYPixels/binY ){
        		height = numYPixels/binY;
        	}
        	//make sure pixels in each quadrant are divisible by binnning
        	if (((height/2)/binY) * binY != height){
    			height = (((height/2)/binY) * binY)*2;
    		}
        	roi->y = ((numYPixels + 1)/2 ) - ((height/2) * binY) ;
        	roi->height = height * binY;
        	roi->y_binning = binY;
        	setIntegerParam(ADMinY, roi->y);
        	setIntegerParam(ADSizeY, height);
        	setIntegerParam(ADBinY, binY);
        }
        else {

        	if (minY < 1){
				minY = 1;
			}
			else if (minY > numYPixels){
				minY = numYPixels;
			}
			roi->y = minY;
			setIntegerParam(ADMinY, minY);
			if (height > (numYPixels - minY + 1)/binY) {
				height = (numYPixels - minY + 1)/binY;
				if (height < 1) {
					height = 1;
				}
			} else if (height < 1 ) {
				height = 1;
			}
			roi->height = height * binY;
			roi->y_binning = binY;
			setIntegerParam(ADSizeY, height);
			setIntegerParam(ADBinY, binY);
        }
        error = Picam_SetParameterRoisValue(currentCameraHandle,
                PicamParameter_Rois, rois);
        if (error != PicamError_None) {
            Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                    &errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Error writing rois %s\n"
            		"(x,y) = (%d, %d), (height, width) = (%d, %d), "
            		"(xbin, ybin) = (%d, %d)\n"
            		, driverName,
					functionName,
                    errorString,
					roi->x,
					roi->y,
					roi->width,
					roi->height,
					roi->x_binning,
					roi->y_binning);

            Picam_DestroyString(errorString);
            return asynError;
        }
    }
    callParamCallbacks();
    Picam_DestroyRoisConstraints(roisConstraints);
    Picam_DestroyRois(rois);
    return (asynStatus) status;
}

/**
 * Set the selected camera based on user input
 */
asynStatus ADPICam::piSetSelectedCamera(asynUser *pasynUser,
        int selectedIndex) {
    int status = asynSuccess;
    const PicamFirmwareDetail *firmwareDetails;
    PicamError error;
    piint numFirmwareDetails = 0;
    const char *modelString;
    const char *interfaceString;
    static const char *functionName = "piSetSelectedCamera";
    char enumString[64];
    char firmwareString[256];
    const char *errorString;

    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s: Selected camera value=%d\n",
            driverName, functionName, selectedIndex);
    if (currentCameraHandle != NULL) {
        status |= piUnregisterRelevantWatch(currentCameraHandle);
        // comment out until method is fixed and register is added
        // piUnregisterConstraintChangeWatch(currentCameraHandle);
        status |= piUnregisterValueChangeWatch(currentCameraHandle);
        error = PicamAdvanced_UnregisterForAcquisitionUpdated(
        		currentDeviceHandle,
                piAcquistionUpdated);
        if (error != PicamError_None){
            const char *errorString;
            Picam_GetEnumerationString(PicamEnumeratedType_Error,
                    error,
                    &errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Error With Picam_UnregisterForAcquisitionUpdate "
            		"%p: %s\n",
                    driverName,
                    functionName,
                    currentCameraHandle,
                    errorString);
            Picam_DestroyString(errorString);
            return (asynStatus)asynError;
        }
    }
    if (selectedCameraIndex >= 0) {
        error = Picam_CloseCamera(currentCameraHandle);
        asynPrint(pasynUser, ASYN_TRACE_FLOW, "Picam_CloseCameraError %d\n",
                error);
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s:%s: Number of available cameras=%d\n", driverName, functionName,
            availableCamerasCount);
    for (int ii = 0; ii < availableCamerasCount; ii++) {
        asynPrint(pasynUser, ASYN_TRACE_FLOW, "Available Camera[%d]\n", ii);
        Picam_GetEnumerationString(PicamEnumeratedType_Model,
                (piint) availableCameraIDs[ii].model, &modelString);
        sprintf(enumString, "%s", modelString);
        asynPrint(pasynUser, ASYN_TRACE_FLOW, "\n---%s\n---%d\n---%s\n---%s\n",
                modelString, availableCameraIDs[ii].computer_interface,
                availableCameraIDs[ii].sensor_name,
                availableCameraIDs[ii].serial_number);
        Picam_DestroyString(modelString);
        //PicamAdvanced_SetUserState(availableCameraIDs[ii].model, this);
    }
    if (selectedIndex < availableCamerasCount) {

        selectedCameraIndex = selectedIndex;
        error = Picam_OpenCamera(&(availableCameraIDs[selectedIndex]),
                &currentCameraHandle);

        if (error != PicamError_None) {
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
					&errorString);
        	asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Trouble Opening Camera. %s\n",
					driverName,
					functionName,
					errorString);
        	Picam_DestroyString(errorString);
        	return (asynStatus)asynError;
        }
        error = PicamAdvanced_GetCameraDevice(currentCameraHandle,
                &currentDeviceHandle);
        if (error != PicamError_None) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Trouble Getting Camera Device\n",
                    driverName,
                    functionName);
        }

        error = PicamAdvanced_RegisterForAcquisitionUpdated(currentDeviceHandle,
                piAcquistionUpdated);
        if (error != PicamError_None){
            const char *errorString;
            Picam_GetEnumerationString(PicamEnumeratedType_Error,
                    error,
                    &errorString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s Error With Picam_RegisterForAcquisitionUpdate: %s\n",
                    driverName,
                    functionName,
                    errorString);
            Picam_DestroyString(errorString);
        }


        Picam_GetEnumerationString(PicamEnumeratedType_Model,
                (piint) availableCameraIDs[selectedIndex].model, &modelString);
        sprintf(enumString, "%s", modelString);
        asynPrint(pasynUser, ASYN_TRACE_FLOW,
                "%s:%s: Selected camera value=%d, %s\n", driverName,
                functionName, selectedIndex, modelString);
        status |= setStringParam(ADModel, modelString);
        Picam_DestroyString(modelString);

        Picam_GetEnumerationString(PicamEnumeratedType_ComputerInterface,
                (piint) availableCameraIDs[selectedIndex].computer_interface,
                &interfaceString);
        sprintf(enumString, "%s", interfaceString);
        status |= setStringParam(PICAM_CameraInterface, enumString);
        Picam_DestroyString(interfaceString);

        status |= setIntegerParam(PICAM_AvailableCameras, selectedIndex);
        status |= setStringParam(PICAM_SensorName,
                availableCameraIDs[selectedIndex].sensor_name);
        status |= setStringParam(PICAM_SerialNumber,
                availableCameraIDs[selectedIndex].serial_number);
        status |= setStringParam(ADSerialNumber,
                availableCameraIDs[selectedIndex].serial_number);

        Picam_GetFirmwareDetails(&(availableCameraIDs[selectedIndex]),
                &firmwareDetails, &numFirmwareDetails);
        asynPrint(pasynUser, ASYN_TRACE_FLOW, "----%d\n", numFirmwareDetails);
        if (numFirmwareDetails > 0) {
            asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s\n",
                    firmwareDetails[0].detail);

            sprintf(firmwareString, "%s", firmwareDetails[0].detail);

            Picam_DestroyFirmwareDetails(firmwareDetails);
            status |= setStringParam(PICAM_FirmwareRevision, firmwareString);
            status |= setStringParam(ADFirmwareVersion, firmwareString);
        } else {
            status |= setStringParam(PICAM_FirmwareRevision, "N/A");
        }
    } else {
        setIntegerParam(PICAM_AvailableCameras, 0);
        setIntegerParam(PICAM_UnavailableCameras, 0);

    }
    callParamCallbacks();

    status |= piRegisterValueChangeWatch(currentCameraHandle);

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();

    status |= piClearParameterExists();
    status |= piClearParameterRelevance();
    status |= piUpdateParameterExists();
    status |= piUpdateParameterRelevance();
    status |= piRegisterRelevantWatch(currentCameraHandle);

    return (asynStatus) status;
}

/**
 * set the selected unavailable camera (to show camera info) based on user
 * input
 */
asynStatus ADPICam::piSetSelectedUnavailableCamera(asynUser *pasynUser,
        int selectedIndex) {
    int status = asynSuccess;
    const PicamFirmwareDetail *firmwareDetails;
    piint numFirmwareDetails = 0;
    const char *modelString;
    const char *interfaceString;
    static const char *functionName = "piSetSelectedUnavailableCamera";
    char enumString[64];
    char firmwareString[256];

    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s: Entry\n", driverName,
            functionName);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s: Selected camera value=%d\n",
            driverName, functionName, selectedIndex);
    if (unavailableCamerasCount == 0) {
        asynPrint(pasynUser, ASYN_TRACE_WARNING,
                "%s:%s: There are no unavailable cameras\n", driverName,
                functionName);
        status |= setStringParam(PICAM_CameraInterfaceUnavailable, enumString);
        status |= setStringParam(PICAM_SensorNameUnavailable, notAvailable);
        status |= setStringParam(PICAM_SerialNumberUnavailable, notAvailable);
        status |= setStringParam(PICAM_FirmwareRevisionUnavailable,
                notAvailable);
        return asynSuccess;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s:%s: Number of available cameras=%d\n", driverName, functionName,
            unavailableCamerasCount);

    Picam_GetEnumerationString(PicamEnumeratedType_Model,
            (piint) unavailableCameraIDs[selectedIndex].model, &modelString);
    sprintf(enumString, "%s", modelString);
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s:%s: Selected camera value=%d, %s\n", driverName, functionName,
            selectedIndex, modelString);
    Picam_DestroyString(modelString);

    Picam_GetEnumerationString(PicamEnumeratedType_ComputerInterface,
            (piint) unavailableCameraIDs[selectedIndex].computer_interface,
            &interfaceString);
    sprintf(enumString, "%s", interfaceString);
    status |= setStringParam(PICAM_CameraInterfaceUnavailable, enumString);
    Picam_DestroyString(interfaceString);

    status |= setIntegerParam(PICAM_AvailableCameras, selectedIndex);
    status |= setStringParam(PICAM_SensorNameUnavailable,
            unavailableCameraIDs[selectedIndex].sensor_name);
    status |= setStringParam(PICAM_SerialNumberUnavailable,
            unavailableCameraIDs[selectedIndex].serial_number);

    Picam_GetFirmwareDetails(&(unavailableCameraIDs[selectedIndex]),
            &firmwareDetails, &numFirmwareDetails);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "----%d\n", numFirmwareDetails);
    if (numFirmwareDetails > 0) {
        asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s\n",
                firmwareDetails[0].detail);

        sprintf(firmwareString, "%s", firmwareDetails[0].detail);

        Picam_DestroyFirmwareDetails(firmwareDetails);
        status |= setStringParam(PICAM_FirmwareRevisionUnavailable,
                firmwareString);
    } else {
        status |= setStringParam(PICAM_FirmwareRevisionUnavailable, "N/A");
    }

    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s:%s: Entry\n", driverName,
            functionName);
    return (asynStatus) status;
}

/**
 * Unregister constraint change callbacks for the currently selected detector
 *
 */
asynStatus ADPICam::piUnregisterConstraintChangeWatch(
        PicamHandle cameraHandle) {
    int status = asynSuccess;
    piint parameterCount;
    const PicamParameter *parameterList;
    PicamError error;
    static const char *functionName = "piUnregisterConstraintChangeWatch";
    const char *errorString;

    error = Picam_GetParameters(currentCameraHandle, &parameterList,
            &parameterCount);
    if (error != PicamError_None) {
    	Picam_GetEnumerationString(PicamEnumeratedType_Error,
    			error,
				&errorString);
    	asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
    			"%s:%s ERROR getting list of parameters\n", driverName, functionName);
    	Picam_DestroyString(errorString);
    	return asynError;
    }
    for (int ii = 0; ii < parameterCount; ii++) {
    	//TODO Need to change to unregister constraint change watch.  Depends
    	// on parameter
    	error = PicamAdvanced_UnregisterForIsRelevantChanged(cameraHandle,
                parameterList[ii], piParameterRelevanceChanged);
    }
    return (asynStatus) status;
}

/**
 * Unregister Parameter Relevance callback for parameters in the currently
 * selected camera
 */
asynStatus ADPICam::piUnregisterRelevantWatch(PicamHandle cameraHandle) {
    int status = asynSuccess;
    piint parameterCount;
    const PicamParameter *parameterList;
    PicamError error;
    static const char *functionName = "piUnregisterRelevantWatch";
    const char *errorString;
    const char *paramString;

    error = Picam_GetParameters(currentCameraHandle, &parameterList,
            &parameterCount);
    if (error != PicamError_None) {
    	Picam_GetEnumerationString(PicamEnumeratedType_Error,
    			error,
				&errorString);
    	asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
    			"%s:%s ERROR getting list of parameters\n", driverName, functionName);
    	Picam_DestroyString(errorString);
    	return asynError;
    }
    for (int ii = 0; ii < parameterCount; ii++) {
        error = PicamAdvanced_UnregisterForIsRelevantChanged(cameraHandle,
                parameterList[ii], piParameterRelevanceChanged);
        if (error != PicamError_None) {
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
					&errorString);
        	Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
        			parameterList[ii],
					&paramString);
        	asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        			"%s:%s Trouble Unregistering RelevantChanged for parameter"
        			"%s.  %s\n",
					driverName,
					__func__,
					paramString,
					errorString);

        	Picam_DestroyString(paramString);
        	Picam_DestroyString(errorString);
        }
    }
    return (asynStatus) status;
}

/**
 * Unregister Parameter Value Change callbacks for the currently selected
 * camera
 */
asynStatus ADPICam::piUnregisterValueChangeWatch(PicamHandle cameraHandle) {
    int status = asynSuccess;
    piint parameterCount;
    const PicamParameter *parameterList;
    PicamValueType valueType;
    PicamError error;
    const char *functionName = "piUnregisterValueChangeWatch";
    pibln doesParamExist;
    const char *errorString;
    const char *paramString;

    error = Picam_GetParameters(currentCameraHandle, &parameterList,
            &parameterCount);
    if (error != PicamError_None) {
    	Picam_GetEnumerationString(PicamEnumeratedType_Error,
    			error,
				&errorString);
    	asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
    			"%s:%s ERROR getting list of parameters\n", driverName, functionName);
    	Picam_DestroyString(errorString);
    	return asynError;
    }
    for (int ii = 0; ii < parameterCount; ii++) {
    	Picam_DoesParameterExist(currentCameraHandle,
    			parameterList[ii],
				&doesParamExist);
    	if (doesParamExist){
			error = Picam_GetParameterValueType(cameraHandle, parameterList[ii],
					&valueType);
			if (error != PicamError_None) {
				asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
				        "%s:%s \n",
				        driverName,
						functionName);
				return asynError;
			}
			switch (valueType) {
			case PicamValueType_Integer:
			case PicamValueType_Boolean:
			case PicamValueType_Enumeration:
				error = PicamAdvanced_UnregisterForIntegerValueChanged(cameraHandle,
						parameterList[ii], piParameterIntegerValueChanged);
				break;
			case PicamValueType_LargeInteger:
				error = PicamAdvanced_UnregisterForLargeIntegerValueChanged(
						cameraHandle, parameterList[ii],
						piParameterLargeIntegerValueChanged);
				break;
			case PicamValueType_FloatingPoint:
				error = PicamAdvanced_UnregisterForFloatingPointValueChanged(
						cameraHandle, parameterList[ii],
						piParameterFloatingPointValueChanged);
				break;
			case PicamValueType_Rois:
				printf("Unregistering ROIS value change\n");
				error = PicamAdvanced_UnregisterForRoisValueChanged(cameraHandle,
						parameterList[ii], piParameterRoisValueChanged);
				break;
			case PicamValueType_Pulse:
				error = PicamAdvanced_UnregisterForPulseValueChanged(cameraHandle,
						parameterList[ii], piParameterPulseValueChanged);
				break;
			case PicamValueType_Modulations:
				error = PicamAdvanced_UnregisterForModulationsValueChanged(
						cameraHandle, parameterList[ii],
						piParameterModulationsValueChanged);
				break;
			default: {
				asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
						"%s:%s Unexpected valueType %d", driverName, functionName,
						valueType);
				return asynError;
			}
			}
			if (error != PicamError_None) {
	        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
	        			error,
						&errorString);
	        	Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
	        			parameterList[ii],
						&paramString);
	        	asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
	        			"%s:%s Trouble Unregistering ValueChangeWatch for "
	        			"parameter %s.  %s\n",
						driverName,
						__func__,
						paramString,
						errorString);

	        	Picam_DestroyString(paramString);
	        	Picam_DestroyString(errorString);
			}
    	}
    }
    return (asynStatus) status;
}

/**
 Update PICAM parameter existance for the current detector
 */
asynStatus ADPICam::piUpdateParameterExists() {
    int status = asynSuccess;
    piint parameterCount;
    const PicamParameter *parameterList;
    const pichar* string;
    Picam_GetParameters(currentCameraHandle, &parameterList, &parameterCount);
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s %d parameters found\n",
            driverName,
            __func__,
            parameterCount);

    for (int ii = 0; ii < parameterCount; ii++) {
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                parameterList[ii], &string);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "---- Found %s\n", string);
        Picam_DestroyString(string);
        status |= piSetParameterExists(pasynUserSelf, parameterList[ii], 1);
    }
    Picam_DestroyParameters(parameterList);
    callParamCallbacks();
    return (asynStatus) status;
}

/**
 Update PICAM parameter relevance for the current detector
 */
asynStatus ADPICam::piUpdateParameterRelevance() {
    int status = asynSuccess;
    piint parameterCount;
    const PicamParameter *parameterList;
    const pichar* string;
    Picam_GetParameters(currentCameraHandle, &parameterList, &parameterCount);
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s %d parameters found\n",
            driverName,
            __func__,
            parameterCount);

    pibln isRelevant;
    int iRelevant;
    for (int ii = 0; ii < parameterCount; ii++) {
        Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                parameterList[ii], &string);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "---- Found %s\n", string);
        Picam_DestroyString(string);
        Picam_IsParameterRelevant(currentCameraHandle, parameterList[ii],
        		&isRelevant);
        iRelevant = (int)isRelevant;
        status |= piSetParameterRelevance(pasynUserSelf, parameterList[ii],
        		iRelevant);
    }
    Picam_DestroyParameters(parameterList);
    callParamCallbacks();
    return (asynStatus) status;
}

/**
 * Reread the list of available detectors to make the list available to the
 * user as detectors come online/go offline
 */
asynStatus ADPICam::piUpdateAvailableCamerasList() {
    int status = asynSuccess;
    const char *functionName = "piUpdateAvailableCamerasList";
    const char *modelString;
    char enumString[64];
    char *strings[MAX_ENUM_STATES];
    int values[MAX_ENUM_STATES];
    int severities[MAX_ENUM_STATES];
    //size_t nElements;
    size_t nIn;


    nIn = 0;
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s availableCamerasCount %d\n", driverName, functionName,
            availableCamerasCount);
    callParamCallbacks();
    for (int ii = 0; ii < availableCamerasCount; ii++) {
        Picam_GetEnumerationString(PicamEnumeratedType_Model,
                (piint) availableCameraIDs[ii].model, &modelString);
        pibln camConnected = false;
        Picam_IsCameraIDConnected(availableCameraIDs, &camConnected);
        sprintf(enumString, "%s", modelString);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "\n%s:%s: \nCamera[%d]\n---%s\n---%d\n---%s\n---%s\n",
                driverName, functionName, ii, modelString,
                availableCameraIDs[ii].computer_interface,
                availableCameraIDs[ii].sensor_name,
                availableCameraIDs[ii].serial_number);
        Picam_DestroyString(modelString);
        strings[nIn] = epicsStrDup(enumString);
        values[nIn] = ii;
        severities[nIn] = 0;
        (nIn)++;
    }

    doCallbacksEnum(strings, values, severities, nIn, PICAM_AvailableCameras,
            0);
    callParamCallbacks();
    return (asynStatus) status;
}

/**
 *
 */
asynStatus ADPICam::piUpdateParameterListValues(
        PicamParameter picamParameter, int driverParameter){
    //const char *functionName = "piUpdateParameterListValues";
    int status = asynSuccess;
    char *strings[MAX_ENUM_STATES];
    int values[MAX_ENUM_STATES];
    int severities[MAX_ENUM_STATES];
    size_t nIn;

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Enter\n",
            driverName,
            __func__);
    for (int ii=0; ii<MAX_ENUM_STATES; ii++) {
        strings[ii] = 0;
    }
    nIn = 0;
    piGenerateListValuesFromCollection(pasynUserSelf, strings,
            values, severities, &nIn,
            driverParameter, picamParameter);

    doCallbacksEnum(strings, values, severities, nIn, driverParameter,
            0);
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s Exit\n",
            driverName,
            __func__);

    return (asynStatus)status;
}

/**
 * Update the list of unavailable Camera list.  This is called as detectors go
 * online/offline
 */
asynStatus ADPICam::piUpdateUnavailableCamerasList() {
    int status = asynSuccess;

    return (asynStatus) status;

}

asynStatus ADPICam::piWriteFloat64RangeType(asynUser *pasynUser,
        epicsFloat64 value,
        int driverParameter,
        PicamParameter picamParameter){
    const char *functionName = "piWriteFloat64RangeType";
    int status = asynSuccess;
    PicamError error;
	PicamConstraintType paramCT;
    PicamValueType valType;
	const PicamRangeConstraint *constraint;
    const pichar *errorString;
    const pichar *paramString;

    error = Picam_GetParameterValueType(currentCameraHandle,
    		picamParameter,
			&valType);
    if (error != PicamError_None) {
    	Picam_GetEnumerationString(PicamEnumeratedType_Error,
    			error,
				&errorString);
    	Picam_GetEnumerationString(PicamEnumeratedType_Error,
    			error,
				&paramString);
    	asynPrint(pasynUser, ASYN_TRACE_ERROR,
    			"%s:%s ERROR: problem getting Parameter value type for"
    			" parameter %s. %s\n",
				driverName,
				__func__,
				paramString,
				errorString);
    	Picam_DestroyString(paramString);
    	Picam_DestroyString(errorString);
    	return asynError;
    }
    if (valType == PicamValueType_FloatingPoint){
    	error = Picam_SetParameterFloatingPointValue(currentCameraHandle,
    			picamParameter,
				value);
    	if (error == PicamError_InvalidParameterValue) {
    		Picam_GetParameterConstraintType( currentCameraHandle,
    				picamParameter,
					&paramCT);
    		if (paramCT == PicamConstraintType_Range) {
    			Picam_GetParameterRangeConstraint(currentCameraHandle,
    					picamParameter,
						PicamConstraintCategory_Required,
						&constraint);
                Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                        picamParameter,
                        &paramString);
    			if ((value < constraint->minimum) ||
    			        (value > constraint->maximum)){
    				asynPrint(pasynUser, ASYN_TRACE_WARNING,
    						"%s,%s Value %f is out of range %f,%f for "
    						"parameter %s\n",
    						driverName,
							functionName,
							value,
							constraint->minimum,
							constraint->maximum,
							paramString);
    				value = (double)constraint->minimum;
    			}
    			error = Picam_SetParameterFloatingPointValue(
    					currentCameraHandle,
						picamParameter,
						value);
        		if (error != PicamError_None) {
                    Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                            &errorString);
                    Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                            picamParameter,
                            &paramString);
                    asynPrint(pasynUser, ASYN_TRACE_ERROR,
                            "%s:%s error writing %f to  %s \n"
                            "Reason %s and not out of range\n",
                            driverName,
                            functionName,
                            value,
                            paramString,
                            errorString);
                    Picam_DestroyString(errorString);
                    return asynError;
                }
                Picam_DestroyString(paramString);

    		}
    	}
    }
    return (asynStatus)status;
}

/**
 * Write int 32 values when a parameters constraint type is range.  This
 * will do some bounds checking according to the range and will not allow
 * setting a value outside of that range.
 *
 */
asynStatus ADPICam::piWriteInt32RangeType(asynUser *pasynUser,
        epicsInt32 value,
        int driverParameter,
        PicamParameter picamParameter){
    const char *functionName = "piWriteInt32RangeType";
    int status = asynSuccess;
    PicamValueType valType;
    PicamError error;
	PicamConstraintType paramCT;
	const PicamRangeConstraint *constraint;
    const pichar *errorString;
    const pichar *paramString;

    error = Picam_GetParameterValueType(currentCameraHandle,
            picamParameter,
            &valType);
    if (error != PicamError_None) {
    	Picam_GetEnumerationString(PicamEnumeratedType_Error,
    			error,
				&errorString);
    	Picam_GetEnumerationString(PicamEnumeratedType_Error,
    			error,
				&paramString);
    	asynPrint(pasynUser, ASYN_TRACE_ERROR,
    			"%s:%s ERROR: problem getting Parameter value type for"
    			" parameter %s. %s\n",
				driverName,
				__func__,
				paramString,
				errorString);
    	Picam_DestroyString(paramString);
    	Picam_DestroyString(errorString);
        return asynError;
    }
    if (valType == PicamValueType_Integer) {
        error = Picam_SetParameterIntegerValue(currentCameraHandle,
                picamParameter, value);
        if (error == PicamError_InvalidParameterValue) {
        	Picam_GetParameterConstraintType(currentCameraHandle,
        			picamParameter,
					&paramCT);
        	if (paramCT == PicamConstraintType_Range){
        		Picam_GetParameterRangeConstraint(currentCameraHandle,
        				picamParameter,
						PicamConstraintCategory_Required,
						&constraint);
                Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                        picamParameter,
                        &paramString);
        		if (value < constraint->minimum){
    				asynPrint(pasynUser, ASYN_TRACE_ERROR,
    						"%s,%s Value %d is out of range %f,%f for "
    						"parameter %s\n",
    						driverName,
							functionName,
							value,
							constraint->minimum,
							constraint->maximum,
							paramString);
	      			value = (int)constraint->minimum;
        		}
        		else if (value > constraint->maximum){
    				asynPrint(pasynUser, ASYN_TRACE_ERROR,
    						"%s,%s Value %d is out of range %f,%f for "
    						"parameter %s\n",
    						driverName,
							functionName,
							value,
							constraint->minimum,
							constraint->maximum,
							paramString);
	        			value = (int)constraint->maximum;
        		}
                error = Picam_SetParameterIntegerValue(currentCameraHandle,
                        picamParameter, value);
        		if (error != PicamError_None) {
                    Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                            &errorString);
                    asynPrint(pasynUser, ASYN_TRACE_ERROR,
                            "%s:%s error writing %d to  %s \n"
                            "Reason %s and not out of range\n",
                            driverName,
                            functionName,
                            value,
                            paramString,
                            errorString);
                    Picam_DestroyString(errorString);
                    return asynError;
                }
                Picam_DestroyString(paramString);
        	}
        }
        else if (error != PicamError_None) {
            Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                    &errorString);
            Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                    picamParameter,
                    &paramString);
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                    "%s:%s error writing %d to  %s \n"
                    "Reason %s\n",
                    driverName,
                    functionName,
                    value,
                    paramString,
                    errorString);
            Picam_DestroyString(paramString);
            Picam_DestroyString(errorString);
            return asynError;
        }
    }
    if (valType == PicamValueType_LargeInteger) {
        pi64s largeValue;
        largeValue = value;
        error = Picam_SetParameterLargeIntegerValue(currentCameraHandle,
                picamParameter, largeValue);
        if (error == PicamError_InvalidParameterValue) {
        	Picam_GetParameterConstraintType(currentCameraHandle,
        			picamParameter,
					&paramCT);
        	if (paramCT == PicamConstraintType_Range){
        		Picam_GetParameterRangeConstraint(currentCameraHandle,
        				picamParameter,
						PicamConstraintCategory_Required,
						&constraint);
                Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                        picamParameter,
                        &paramString);
        		if (value < constraint->minimum){
    				asynPrint(pasynUser, ASYN_TRACE_ERROR,
    						"%s,%s Value %d is out of range %f,%f for "
    						"parameter %s\n",
    						driverName,
							functionName,
							value,
							constraint->minimum,
							constraint->maximum,
							paramString);
        			value = (int)constraint->minimum;
        		}
        		else if (value > constraint->maximum){
    				asynPrint(pasynUser, ASYN_TRACE_ERROR,
    						"%s,%s Value %d is out of range %f,%f for "
    						"parameter %s\n",
    						driverName,
							functionName,
							value,
							constraint->minimum,
							constraint->maximum,
							paramString);
        			value = (int)constraint->maximum;
        		}
        		largeValue = (pi64s)value;
                error = Picam_SetParameterLargeIntegerValue(currentCameraHandle,
                        picamParameter, largeValue);
        		if (error != PicamError_None) {
                    Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                            &errorString);
                    asynPrint(pasynUser, ASYN_TRACE_ERROR,
                            "%s:%s error writing %d to  %s \n"
                            "Reason %s and not out of range\n",
                            driverName,
                            functionName,
                            value,
                            paramString,
                            errorString);
                    Picam_DestroyString(errorString);
                    return asynError;
        		}
                Picam_DestroyString(paramString);
        	}
        }
        if (error != PicamError_None) {
            Picam_GetEnumerationString(PicamEnumeratedType_Error, error,
                    &errorString);
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                    "%s:%s error writing %ld to %s\n"
                    "Reason %s\n",
                    driverName,
                    functionName,
                    largeValue,
                    paramString,
                    errorString);
            Picam_DestroyString(errorString);
            return asynError;
        }

    }
    return (asynStatus)status;
}

/**
 * Write an int 32 value when the constraint type is a collection.  In this
 * case the output will be a string associated with the collection enum
 * value.
 */
asynStatus ADPICam::piWriteInt32CollectionType(asynUser *pasynUser,
        epicsInt32 value,
        int driverParameter,
        PicamParameter picamParameter){
    const char *functionName = "piWriteInt32CollectionType";
    int status = asynSuccess;
    PicamValueType valType;
    PicamError error;
    const char *paramString;
    const char *errorString;

    error = Picam_GetParameterValueType(currentCameraHandle,
            picamParameter,
            &valType);
    if (error != PicamError_None) {
    	Picam_GetEnumerationString(PicamEnumeratedType_Error,
    			error,
				&errorString);
    	Picam_GetEnumerationString(PicamEnumeratedType_Error,
    			error,
				&paramString);
    	asynPrint(pasynUser, ASYN_TRACE_ERROR,
    			"%s:%s ERROR: problem getting Parameter value type for"
    			" parameter %s. %s\n",
				driverName,
				__func__,
				paramString,
				errorString);
    	Picam_DestroyString(paramString);
    	Picam_DestroyString(errorString);
        return asynError;
    }
    if (valType == PicamValueType_Boolean) {
        error = Picam_SetParameterIntegerValue(currentCameraHandle,
                picamParameter, value);
        if (error != PicamError_None) {
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
    				&errorString);
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
    				&paramString);
        	asynPrint(pasynUser, ASYN_TRACE_ERROR,
        			"%s:%s ERROR: problem setting Parameter value for"
        			" parameter %s trying to set value to %d. %s\n",
    				driverName,
    				__func__,
    				paramString,
					value,
    				errorString);
        	Picam_DestroyString(paramString);
        	Picam_DestroyString(errorString);
            return asynError;
        }
    }
    if (valType == PicamValueType_Integer) {
        error = Picam_SetParameterIntegerValue(currentCameraHandle,
                picamParameter, value);
        if (error != PicamError_None) {
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
    				&errorString);
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
    				&paramString);
        	asynPrint(pasynUser, ASYN_TRACE_ERROR,
        			"%s:%s ERROR: problem setting Parameter value for"
        			" parameter %s trying to set value to %d. %s\n",
    				driverName,
    				__func__,
    				paramString,
    				value,
    				errorString);
        	Picam_DestroyString(paramString);
        	Picam_DestroyString(errorString);
            return asynError;
        }
    }
    if (valType == PicamValueType_LargeInteger) {
        pi64s largeVal = value;
        error = Picam_SetParameterLargeIntegerValue(currentCameraHandle,
                picamParameter, largeVal);
        if (error != PicamError_None) {
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
    				&errorString);
        	Picam_GetEnumerationString(PicamEnumeratedType_Error,
        			error,
    				&paramString);
        	asynPrint(pasynUser, ASYN_TRACE_ERROR,
        			"%s:%s ERROR: problem setting Parameter value for"
        			" parameter %s trying to set value to %ld. %s\n",
    				driverName,
    				__func__,
    				paramString,
                    largeVal,
    				errorString);
        	Picam_DestroyString(paramString);
        	Picam_DestroyString(errorString);
            return asynError;
        }
    }
    else if (valType == PicamValueType_FloatingPoint){
        const PicamCollectionConstraint* paramCollection;
        error = Picam_GetParameterCollectionConstraint(currentCameraHandle,
                picamParameter, PicamConstraintCategory_Capable,
                &paramCollection);
        if (error == PicamError_None) {
            Picam_SetParameterFloatingPointValue(currentCameraHandle,
                    picamParameter,
                    paramCollection->values_array[value]);
        } else {
            const char *paramString;
            Picam_GetEnumerationString(PicamEnumeratedType_Parameter,
                    picamParameter,
                    &paramString);
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s No collection values available for %s list",
                    driverName, functionName, paramString);
            Picam_DestroyString(paramString);
        }

    }
    else if (valType == PicamValueType_Enumeration) {
        error = Picam_SetParameterIntegerValue(currentCameraHandle,
                picamParameter, value);
        if (error != PicamError_None) {
            //TODO
            return asynError;
        }
    }
    return (asynStatus)status;
}


/**
 * Callback when a new Image event is seen.  Call the driver's method
 * piHanfleNewImageTask
 */
static void piHandleNewImageTaskC(void *drvPvt)
{
    ADPICam *pPvt = (ADPICam *)drvPvt;

    pPvt->piHandleNewImageTask();
}

/**
 * Handler class for recieving new images.  This runs in a thread separate
 * from the picam driver thread to avoid collisions.  Acquisition in the
 * picam thread will signal this thread as soon as possible when new images
 * are seen.
 */
void ADPICam::piHandleNewImageTask(void)
{
    const char * functionName = "piHandleNewImageTask";
    int imageMode;
    int imagesCounter;
    int numImages;
    int arrayCounter;
    int arrayCallbacks;
    NDArrayInfo arrayInfo;
    PicamError error;
    int useDriverTimestamps;
    int useFrameTracking;
    int frameTrackingBitDepth;
    pi64s timeStampValue;
    pi64s *pTimeStampValue;
    pi64s *pFrameValue;
    int timeStampsRel;
    int trackFramesRel;
    int timeStampBitDepth;
    int timeStampResolution;
    int frameSize;
    int numTimeStamps;
    epicsEventWaitStatus newImageTimeoutStatus = epicsEventWaitTimeout;
    double imageTimeout = 0.000001;

  while (true) {
    unlock();
    dataLock.unlock();
	while ( newImageTimeoutStatus ) {
		newImageTimeoutStatus = epicsEventWaitWithTimeout(piHandleNewImageEvent,
			imageTimeout);
		if (!imageThreadKeepAlive){
			  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
					  "%s:%s Image handling thread has been terminated.\n",
					  driverName,
					  __func__);
			  return;
		}
	}
	newImageTimeoutStatus = epicsEventWaitTimeout;
    dataLock.lock();
	lock();
	getIntegerParam(PICAM_TimeStampBitDepthRelevant, &timeStampsRel);
	if (timeStampsRel){
	    getIntegerParam(PICAM_TimeStamps, &useDriverTimestamps);
	}else {
	    useDriverTimestamps = false;
	}
	getIntegerParam(PICAM_FrameTrackingBitDepthRelevant, &trackFramesRel);
	if (trackFramesRel){
	    getIntegerParam(PICAM_TrackFrames, &useFrameTracking);
	}else {
	    useFrameTracking = false;
	}
    	if (acqStatusErrors == PicamAcquisitionErrorsMask_None) {
            if (acqStatusRunning ||
                    (!acqStatusRunning && (acqAvailableReadoutCount != 0) )) {
                getIntegerParam(ADImageMode, &imageMode);
                getIntegerParam(ADNumImages, &numImages);
                getIntegerParam(ADNumImagesCounter, &imagesCounter);
                imagesCounter++;
                setIntegerParam(ADNumImagesCounter, imagesCounter);
                asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                        "Acquire, Running %s, errors %d, rate %f, "
                		"availableDataCount %ld\n",
                        acqStatusRunning ? "True" : "False",
                        acqStatusErrors,
                        acqStatusReadoutRate, acqAvailableReadoutCount);
                /* Update the image */
                /* First release the copy that we held onto last time */
                if (this->pArrays[0]) {
                    this->pArrays[0]->release();
                }

                getIntegerParam(NDArrayCallbacks, &arrayCallbacks);
                if (arrayCallbacks) {
                    /* Allocate a new array */
                    this->pArrays[0] = pNDArrayPool->alloc(2, imageDims,
                            imageDataType, 0,
                            NULL);
                    if (this->pArrays[0] != NULL) {
						if (acqStatusErrors != PicamAcquisitionErrorsMask_None) {
							const char *acqStatusErrorString;
							Picam_GetEnumerationString(
									PicamEnumeratedType_AcquisitionErrorsMask,
									acqStatusErrors, &acqStatusErrorString);
							asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
									"%s:%s Error found during acquisition: %s",
									driverName,
									functionName,
									acqStatusErrorString);
							Picam_DestroyString(acqStatusErrorString);
						}
						pibln overran;
						error = PicamAdvanced_HasAcquisitionBufferOverrun(
								currentDeviceHandle, &overran);
						if (overran) {
							asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
									"%s:%s Overrun in acquisition buffer",
									driverName, functionName);
						}
						pImage = this->pArrays[0];
						pImage->getInfo(&arrayInfo);
						// Copy data from the input to the output
						memcpy(pImage->pData, acqAvailableInitialReadout,
								arrayInfo.totalBytes);
						getIntegerParam(NDArrayCounter, &arrayCounter);
						arrayCounter++;
						setIntegerParam(NDArrayCounter, arrayCounter);
						// Get timestamp from the driver if requested
						if (timeStampsRel) {
                            getIntegerParam(PICAM_TimeStampBitDepth,
                                    &timeStampBitDepth);
                            getIntegerParam(PICAM_TimeStampResolution,
                                    &timeStampResolution);
						}
                        Picam_GetParameterIntegerValue(currentCameraHandle,
								PicamParameter_FrameSize,
								&frameSize);
						if (!useDriverTimestamps){
							updateTimeStamps(pImage);
						}
						else {
							pTimeStampValue =
									(pi64s*) ((pibyte *)acqAvailableInitialReadout
											+ frameSize);
							timeStampValue = *pTimeStampValue;
							asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
									"%s%s TimeStamp %ld  Res %d frame size %d "
									"timestamp %f\n",
									driverName,
									functionName,
									timeStampValue,
									timeStampResolution,
									frameSize,
									(double)timeStampValue /(double)timeStampResolution);
							updateTimeStamps(pImage);
							pImage->timeStamp = (double)timeStampValue /
									(double)timeStampResolution;
						}
						// use frame tracking for UniqueID if requested
						if (!useFrameTracking) {
							pImage->uniqueId = arrayCounter;
						}
						else {
							asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
									"%s:%s  TimeStamps 0X%X\n",
									driverName,
									functionName,
									useDriverTimestamps);
							// Frame tracking info follows data and time stamps.
							// need to determine the correct number of time
							// stamps to skip
							if ((useDriverTimestamps ==
									PicamTimeStampsMask_None)) {
								numTimeStamps = 0;
							}
							else if ((useDriverTimestamps ==
										PicamTimeStampsMask_ExposureStarted) ||
								(useDriverTimestamps ==
										PicamTimeStampsMask_ExposureEnded) ) {
								numTimeStamps = 1;
							}
							else  {
								numTimeStamps = 2;
							}
							getIntegerParam(PICAM_FrameTrackingBitDepth,
									&frameTrackingBitDepth);
							switch (frameTrackingBitDepth){
							case 64:
								pFrameValue =
										(pi64s*) ((pibyte *)acqAvailableInitialReadout
										+ frameSize
										+ (numTimeStamps * timeStampBitDepth/8));
								asynPrint (pasynUserSelf, ASYN_TRACE_FLOW,
										"%s:%s Frame tracking bit depth %d"
										" timeStampBitDepth %d, frameValue %ld "
										" readout count %ld\n",
										driverName,
										functionName,
										frameTrackingBitDepth,
										timeStampBitDepth,
										*pFrameValue,
										acqAvailableReadoutCount);
								pImage->uniqueId = (int)(*pFrameValue);
								break;
							}
						}

						/* Get attributes that have been defined for this driver */
						getAttributes(pImage->pAttributeList);

						asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
								"%s:%s: calling imageDataCallback\n",
								driverName,
								functionName);

						doCallbacksGenericPointer(pImage, NDArrayData, 0);
                    }
                    else {
                        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                                "%s:%s error allocating buffer\n", driverName,
                                functionName);
                        piAcquireStop();
                        setIntegerParam(ADStatus, ADStatusError);
                        callParamCallbacks();
                    }
                }

            }

            else if (!(acqStatusRunning) && acqAvailableReadoutCount == 0) {
                const char *errorMaskString;
                Picam_GetEnumerationString(
                		PicamEnumeratedType_AcquisitionErrorsMask,
                        acqStatusErrors,
						&errorMaskString);
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                        "Acquire1, Running %s, errors %s, rate %f, array "
                        "counter %d\n",
                        acqStatusRunning ? "True":"false",
                                errorMaskString,
                                acqStatusReadoutRate,
                                arrayCounter);
                Picam_DestroyString(errorMaskString);
                piAcquireStop();
            }
    }
    else if (acqStatusErrors != PicamAcquisitionErrorsMask_None) {
        const char *errorMaskString;
        Picam_GetEnumerationString(PicamEnumeratedType_AcquisitionErrorsMask,
                acqStatusErrors, &errorMaskString);
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                "Acquire2, Running %s, errors %s, rate %f\n",
                acqStatusRunning ? "True":"false",
                        errorMaskString,
                        acqStatusReadoutRate);
        Picam_DestroyString(errorMaskString);
        piAcquireStop();
    }
    callParamCallbacks();
    if (((imageMode == ADImageMultiple)
            && (imagesCounter >= numImages)) ||
            ((imageMode == ADImageSingle) &&
                    (imagesCounter == 1))) {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,
                "%s:%s Calling piAcquireStop()\n",
                driverName, __func__);
        lock();
        piAcquireStop();
        unlock();
    }
  }

}

/* Code for iocsh registration */

/* PICamConfig */
static const iocshArg PICamConfigArg0 = { "Port name", iocshArgString };
static const iocshArg PICamConfigArg1 = { "maxBuffers", iocshArgInt };
static const iocshArg PICamConfigArg2 = { "maxMemory", iocshArgInt };
static const iocshArg PICamConfigArg3 = { "priority", iocshArgInt };
static const iocshArg PICamConfigArg4 = { "stackSize", iocshArgInt };
static const iocshArg * const PICamConfigArgs[] = { &PICamConfigArg0,
        &PICamConfigArg1, &PICamConfigArg2, &PICamConfigArg3, &PICamConfigArg4 };

static const iocshFuncDef configPICam = { "PICamConfig", 5, PICamConfigArgs };

static const iocshArg PICamAddDemoCamArg0 =
        { "Demo Camera name", iocshArgString };
static const iocshArg * const PICamAddDemoCamArgs[] = { &PICamAddDemoCamArg0 };

static const iocshFuncDef addDemoCamPICam = { "PICamAddDemoCamera", 1,
        PICamAddDemoCamArgs };

static void configPICamCallFunc(const iocshArgBuf *args) {
    PICamConfig(args[0].sval, args[1].ival, args[2].ival, args[3].ival,
            args[4].ival);
}

static void addDemoCamPICamCallFunc(const iocshArgBuf *args) {
    PICamAddDemoCamera(args[0].sval);
}

static void PICamRegister(void) {
    iocshRegister(&configPICam, configPICamCallFunc);
    iocshRegister(&addDemoCamPICam, addDemoCamPICamCallFunc);
}

extern "C" {
epicsExportRegistrar(PICamRegister);
}
