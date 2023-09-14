/*
 * idle.c
 *
 *  Created on: Sep 14, 2023
 *      Author: luke
 */

#include <string.h>

#include "dx/eth2usb/active_servo_class.h"
#include "dx/eth2usb/active_servo_class_states/writing.h"
#include "logging.h"

USBH_StatusTypeDef DX_USB_ActiveServoClass_ReadingState_Entry(USBH_HandleTypeDef *phost)
{
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	DX_ActiveServoClass_ReadingState_t *readingState = &handle->readingState;

	USBH_StatusTypeDef usbhStatus = USBH_OK;

	mlog("Entering reading state");

	readingState->reading = false;

	return usbhStatus;

}

USBH_StatusTypeDef DX_USB_ActiveServoClass_ReadingState_Do_HandleDone(USBH_HandleTypeDef *phost)
{
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	DX_ActiveServoClass_Rsp_TypeDef rsp;

	USBH_StatusTypeDef usbhStatus = USBH_OK;

	mlog("USB host finished reading");

	rsp.status = DX__ACTIVE_SERVO_CLASS__OK;
	osMessageQueuePut(handle->rspMsgQueueId, &rsp, 0U, 0U);

	handle->nextState = DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__IDLE;

	return usbhStatus;
}

USBH_StatusTypeDef DX_USB_ActiveServoClass_ReadingState_Do(USBH_HandleTypeDef *phost)
{
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	DX_ActiveServoClass_ReadingState_t *readingState = &handle->readingState;

	USBH_StatusTypeDef usbhStatus = USBH_OK;
	USBH_URBStateTypeDef usbhUrbState = USBH_URB_IDLE;

	if (readingState->reading) {
		usbhUrbState = USBH_LL_GetURBState(phost, handle->inPipeNo);

		switch (usbhUrbState) {
		case USBH_URB_DONE:
			DX_USB_ActiveServoClass_ReadingState_Do_HandleDone(phost);
			break;
		case USBH_URB_STALL:
		{
			mlog("USB host got stall condition reported");

			handle->state = DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__ERROR;

			break;
		}
		default:
			break;
		}
	} else {
		memset(handle->cmd.in, 0, handle->inEpMaxPktSize);

		USBH_LL_SetToggle(phost, handle->inPipeNo, 1U);
		usbhStatus = USBH_BulkReceiveData(phost, handle->cmd.in, handle->inEpMaxPktSize,
				handle->inPipeNo);

		mlog("Reading bulk data");

		if (usbhStatus != USBH_OK) {
			mlog("Failed to read bulk data, USB host status: %d", usbhStatus);

			usbhStatus = USBH_FAIL;
		}

		readingState->reading = true;
	}

	return usbhStatus;
}

USBH_StatusTypeDef DX_USB_ActiveServoClass_ReadingState_Exit(USBH_HandleTypeDef *phost)
{
	USBH_StatusTypeDef status = USBH_OK;

	return status;
}
