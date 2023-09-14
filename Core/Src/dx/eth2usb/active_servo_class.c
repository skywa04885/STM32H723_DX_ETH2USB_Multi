/*
 * active_servo_class.c
 *
 *  Created on: Sep 11, 2023
 *      Author: luke
 */

#include "dx/eth2usb/active_servo_class.h"
#include "dx/eth2usb/active_servo_class_states/idle.h"
#include "dx/eth2usb/active_servo_class_states/writing.h"
#include "dx/eth2usb/active_servo_class_states/reading.h"
#include "settings.h"
#include "logging.h"

static USBH_StatusTypeDef DX_USB_ActiveServoClass_InterfaceInit(
		USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef DX_USB_ActiveServoClass_InterfaceDeInit(
		USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef DX_USB_ActiveServoClass_Process(
		USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef DX_USB_ActiveServoClass_ClassRequest(
		USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef DX_USB_ActiveServoClass_SOFProcess(
		USBH_HandleTypeDef *phost);

USBH_ClassTypeDef gDxActiveServoClass = { .Name = "Active Servo",
		.ClassCode = DX_ETH2USB__USB_DEVICE__CLASS_CODE, .Init =
				DX_USB_ActiveServoClass_InterfaceInit, .DeInit =
				DX_USB_ActiveServoClass_InterfaceDeInit, .Requests =
				DX_USB_ActiveServoClass_ClassRequest, .BgndProcess =
				DX_USB_ActiveServoClass_Process, .SOFProcess =
				DX_USB_ActiveServoClass_SOFProcess, .pData = NULL, };

/// The function that gets called to initialize the interface.
static USBH_StatusTypeDef DX_USB_ActiveServoClass_InterfaceInit(
		USBH_HandleTypeDef *phost) {
	DX_ActiveServoClass_HandleTypeDef *handle = NULL;
	USBH_InterfaceDescTypeDef *interface = NULL;
	USBH_StatusTypeDef status = USBH_OK;
	uint8_t interfaceIndex = 0U;

	// Finds the index of the specific interface we need.
	interfaceIndex = USBH_FindInterface(phost,
	DX_ETH2USB__USB_DEVICE__INTERFACE__CLASS_CODE,
	DX_ETH2USB__USB_DEVICE__INTERFACE__SUB_CLASS_CODE,
	DX_ETH2USB__USB_DEVICE__INTERFACE__PROTOCOL_CODE);
	if ((interfaceIndex == 0xFFU)
			|| (interfaceIndex >= USBH_MAX_NUM_INTERFACES)) {
		mlog("Cannot Find the interface for %s class.",
				phost->pActiveClass->Name);
		return USBH_FAIL;
	}
	mlog("Found interface %d for class", interfaceIndex);

	// Gets the specific interface from the index.
	interface = &phost->device.CfgDesc.Itf_Desc[interfaceIndex];

	// Selects the interface.
	status = USBH_SelectInterface(phost, interfaceIndex);
	if (status != USBH_OK) {
		mlog("Failed to select interface");
		return USBH_FAIL;
	}
	mlog("Selected interface %d", interfaceIndex);

	// Makes sure that the third end-point is of type input.
	if (!(interface->Ep_Desc[2U].bEndpointAddress & 0x80U)) {
		mlog("Third end-point of the interface is not of type IN");
		return USBH_FAIL;
	}
	mlog("Third end-point is of type IN and has address %02x", interface->Ep_Desc[2U].bEndpointAddress);

	// Makes sure that the fourth end-point is of type output.
	if (interface->Ep_Desc[3U].bEndpointAddress & 0x80U) {
		mlog("Fourth end-point of the interface is not of type OUT");
	}
	mlog("Fourth end-point is of type OUT and has address %02x", interface->Ep_Desc[3U].bEndpointAddress);

	// Allocates the memory needed for the handle.
	phost->pActiveClass->pData = USBH_malloc(
			sizeof(DX_ActiveServoClass_HandleTypeDef));
	handle = (DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	if (handle == NULL) {
		mlog("Failed to allocate memory for ActiveServoClass handle");
		return USBH_FAIL;
	}

	// Clears the memory of the handle to prevent possible UB.
	USBH_memset(handle, 0, sizeof(DX_ActiveServoClass_HandleTypeDef));

	// Gets the address and the max packet size of the input end-point.
	handle->inEpAddr = interface->Ep_Desc[2U].bEndpointAddress;
	handle->inEpMaxPktSize = interface->Ep_Desc[2U].wMaxPacketSize;

	// Gets the address and the max packet size of the output end-point.
	handle->outEpAddr = interface->Ep_Desc[3U].bEndpointAddress;
	handle->outEpMaxPktSize = interface->Ep_Desc[3U].wMaxPacketSize;

	// Allocates the memory for the pipes.
	// TODO: figure out how to handle possible errors here.
	handle->inPipeNo = USBH_AllocPipe(phost, handle->inEpAddr);
	handle->outPipeNo = USBH_AllocPipe(phost, handle->outEpAddr);

	// Opens the output pipe.
	status = USBH_OpenPipe(phost, handle->outPipeNo, handle->outEpAddr,
			phost->device.address, phost->device.speed,
			USB_EP_TYPE_BULK, handle->outEpMaxPktSize);
	if (status != USBH_OK) {
		mlog("Failed to open output pipe");
		return USBH_FAIL;
	}
	mlog("Created IN pipe with address %02x on end-point with address %02x", handle->inPipeNo,
			handle->inEpAddr);

	// Opens the input pipe.
	status = USBH_OpenPipe(phost, handle->inPipeNo, handle->inEpAddr,
			phost->device.address, phost->device.speed,
			USB_EP_TYPE_BULK, handle->inEpMaxPktSize);
	if (status != USBH_OK) {
		mlog("Failed to open input pipe");
		return USBH_FAIL;
	}
	mlog("Created OUT pipe with address %02x on end-point with address %02x", handle->outPipeNo,
			handle->outEpAddr);

	// Sets the toggle for the input and output pipes (I have no clue why we need to set this).
	//  I'm basically basing everything on the USBH_MSC class code.
	USBH_LL_SetToggle(phost, handle->inPipeNo, 1U);
	USBH_LL_SetToggle(phost, handle->outPipeNo, 0U);

	// Creates the availability mutex.
	handle->availabilityMutexId = osMutexNew(NULL);
	if (handle->availabilityMutexId == NULL) {
		mlog("Failed to create availability mutex");
		return USBH_FAIL;
	}

	// Creates the command message queue.
	handle->cmdMsgQueueId = osMessageQueueNew(1U,
			sizeof(DX_ActiveServoClass_Cmd_TypeDef), NULL);
	if (handle->cmdMsgQueueId == NULL) {
		mlog("Failed to create command message queue");
		return USBH_FAIL;
	}

	// Creates the response message queue.
	handle->rspMsgQueueId = osMessageQueueNew(1U,
			sizeof(DX_ActiveServoClass_Rsp_TypeDef), NULL);
	if (handle->rspMsgQueueId == NULL) {
		mlog("Failed to create response message queue");
		return USBH_FAIL;
	}

	// Sets started to false, to let the state machine know
	//  it should still start.
	handle->started = false;

	// I think the setup was okay?
	return USBH_OK;
}

static USBH_StatusTypeDef DX_USB_ActiveServoClass_InterfaceDeInit(
		USBH_HandleTypeDef *phost) {
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	USBH_StatusTypeDef status = USBH_OK;
	osStatus_t osStatus = osOK;

	// Doing this instead of the way being done in the MSC example.
	//  Basically, the one in the example, can cause UB.
	if (handle == NULL)
		return USBH_OK;

	// Closes the output pipe if it's opened.
	if ((handle->outPipeNo) != 0U) {
		status = USBH_ClosePipe(phost, handle->outPipeNo);
		if (status != USBH_OK) {
			mlog("Failed to close output pipe");
			return USBH_FAIL;
		}

		status = USBH_FreePipe(phost, handle->outPipeNo);
		if (status != USBH_OK) {
			mlog("Failed to free output pipe");
			return USBH_FAIL;
		}

		handle->outPipeNo = 0U;
	}

	// Closes the input pipe if it's opened.
	if ((handle->inPipeNo) != 0U) {
		status = USBH_ClosePipe(phost, handle->inPipeNo);
		if (status != USBH_OK) {
			mlog("Failed to close input pipe");
			return USBH_FAIL;
		}

		status = USBH_FreePipe(phost, handle->inPipeNo);
		if (status != USBH_OK) {
			mlog("Failed to free input pipe");
			return USBH_FAIL;
		}

		handle->inPipeNo = 0U;
	}

	// Frees the availability mutex if there.
	if (handle->availabilityMutexId != NULL) {
		osStatus = osMutexDelete(handle->availabilityMutexId);
		if (osStatus != osOK) {
			mlog("Failed to free the availability mutex");
			return USBH_FAIL;
		}
	}

	// Frees the command message queue if it's created.
	if (handle->cmdMsgQueueId != NULL) {
		osStatus = osMessageQueueDelete(handle->cmdMsgQueueId);
		if (osStatus != osOK) {
			mlog("Failed to free command message queue");
			return USBH_FAIL;
		}
	}

	// Frees the response message queue if it's created.
	if (handle->rspMsgQueueId != NULL) {
		osStatus = osMessageQueueDelete(handle->rspMsgQueueId);
		if (osStatus != osOK) {
			mlog("Failed to free response message queue");
			return USBH_FAIL;
		}
	}

	// Frees the handle.
	USBH_free(phost->pActiveClass->pData);
	phost->pActiveClass->pData = NULL;

	return USBH_OK;
}

//static USBH_StatusTypeDef DX_USB_ActiveServoClass_Process_Idle(
//		USBH_HandleTypeDef *phost) {

//}

static USBH_StatusTypeDef DX_USB_ActiveServoClass_Process_PerformCurrentEntry(USBH_HandleTypeDef *phost) {
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	USBH_StatusTypeDef status = USBH_OK;

	switch (handle->state) {
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__IDLE:
		status = DX_USB_ActiveServoClass_IdleState_Entry(phost);
		break;
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__WRITING:
		status = DX_USB_ActiveServoClass_WritingState_Entry(phost);
		break;
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__READING:
		status = DX_USB_ActiveServoClass_ReadingState_Entry(phost);
		break;
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__ERROR:
		break;
	}

	return status;
}

static USBH_StatusTypeDef DX_USB_ActiveServoClass_Process_PerformCurrentDo(USBH_HandleTypeDef *phost) {
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	USBH_StatusTypeDef status = USBH_OK;

	switch (handle->state) {
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__IDLE:
		status = DX_USB_ActiveServoClass_IdleState_Do(phost);
		break;
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__WRITING:
		status = DX_USB_ActiveServoClass_WritingState_Do(phost);
		break;
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__READING:
		status = DX_USB_ActiveServoClass_ReadingState_Do(phost);
		break;
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__ERROR:
		break;
	}

	return status;
}

static USBH_StatusTypeDef DX_USB_ActiveServoClass_Process_PerformCurrentExit(USBH_HandleTypeDef *phost) {
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	USBH_StatusTypeDef status = USBH_OK;

	switch (handle->state) {
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__IDLE:
		status = DX_USB_ActiveServoClass_IdleState_Exit(phost);
		break;
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__WRITING:
		status = DX_USB_ActiveServoClass_WritingState_Exit(phost);
		break;
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__READING:
		status = DX_USB_ActiveServoClass_ReadingState_Exit(phost);
		break;
	case DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__ERROR:
		break;
	}

	return status;
}

static USBH_StatusTypeDef DX_USB_ActiveServoClass_Process_PerformStateTransition(USBH_HandleTypeDef *phost) {
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	USBH_StatusTypeDef status = USBH_OK;

	if (handle->nextState == handle->state)
		return status;

	status = DX_USB_ActiveServoClass_Process_PerformCurrentExit(phost);
	if (status != USBH_OK)
		return status;

	handle->state = handle->nextState;

	status = DX_USB_ActiveServoClass_Process_PerformCurrentEntry(phost);
	if (status != USBH_OK)
		return status;

	return status;
}

static USBH_StatusTypeDef DX_USB_ActiveServoClass_Process(
		USBH_HandleTypeDef *phost) {
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;

	USBH_StatusTypeDef status = USBH_OK;

	// Starts the state machine if needed.
	if (!handle->started) {
		mlog("Starting state machine");

		handle->state = handle->nextState = DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__IDLE;
		handle->started = true;

		status = DX_USB_ActiveServoClass_Process_PerformCurrentEntry(phost);

		return status;
	}

	// Performs the state transition if needed.
	status = DX_USB_ActiveServoClass_Process_PerformStateTransition(phost);
	if (status != USBH_OK)
		return status;

	// Performs the do of the current state.
	status = DX_USB_ActiveServoClass_Process_PerformCurrentDo(phost);
	if (status != USBH_OK)
		return status;

	return status;
}

static USBH_StatusTypeDef DX_USB_ActiveServoClass_ClassRequest(
		USBH_HandleTypeDef *phost) {
	return USBH_OK;
}

static USBH_StatusTypeDef DX_USB_ActiveServoClass_SOFProcess(
		USBH_HandleTypeDef *phost) {
	// We don't need to do anything periodically.
	return USBH_OK;
}

DX_ActiveServoClass_StatusTypeDef DX_ActiveServoClass_Cmd(
		USBH_HandleTypeDef *phost, uint8_t *out, uint8_t *in) {
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	DX_ActiveServoClass_Cmd_TypeDef cmd;
	DX_ActiveServoClass_Rsp_TypeDef rsp;
	osStatus_t osStatus = osOK;

	cmd.out = out;
	cmd.in = in;

	mlog("Acquiring availability mutex");
	osStatus = osMutexAcquire(handle->availabilityMutexId, osWaitForever);
	if (osStatus != osOK) {
		USBH_DbgLog("Failed to acquire the availability mutex");
		return DX__ACTIVE_SERVO_CLASS__ERR;
	}
	mlog("Acquired availability mutex");

	mlog("Putting command in message queue");
	osStatus = osMessageQueuePut(handle->cmdMsgQueueId, &cmd, 0U,
			osWaitForever);
	if (osStatus != osOK) {
		USBH_DbgLog("Failed to put command inside message queue");
		return DX__ACTIVE_SERVO_CLASS__ERR;
	}
	mlog("Put command in message queue");

		while (osMessageQueueGetCount(handle->rspMsgQueueId) == 0) {
		  phost->os_msg = (uint32_t)USBH_CLASS_EVENT;
		  (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, 0U);
		}

	mlog("Waiting for response");
	osStatus = osMessageQueueGet(handle->rspMsgQueueId, &rsp, 0U,
			osWaitForever);
	if (osStatus != osOK) {
		USBH_DbgLog("Failed to get response from message queue");
		return DX__ACTIVE_SERVO_CLASS__ERR;
	}
	mlog("Received response");

	osStatus = osMutexRelease(handle->availabilityMutexId);
	if (osStatus != osOK) {
		USBH_DbgLog("Failed to release the availability mutex");
		return DX__ACTIVE_SERVO_CLASS__ERR;
	}

	return rsp.status;
}

