/*
 * idle.c
 *
 *  Created on: Sep 14, 2023
 *      Author: luke
 */

#include "logging.h"
#include "dx/eth2usb/active_servo_class.h"
#include "dx/eth2usb/active_servo_class_states/writing.h"
#include "logging.h"

USBH_StatusTypeDef DX_USB_ActiveServoClass_WritingState_Entry(USBH_HandleTypeDef *phost)
{
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	DX_ActiveServoClass_WritingState_t *writingState = &handle->writingState;

	USBH_StatusTypeDef usbhStatus = USBH_OK;

	mlog("Entering writing state");

	writingState->written = false;

	return usbhStatus;
}

USBH_StatusTypeDef DX_USB_ActiveServoClass_WritingState_Do(USBH_HandleTypeDef *phost)
{
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	DX_ActiveServoClass_WritingState_t *writingState = &handle->writingState;

	USBH_StatusTypeDef usbhStatus = USBH_OK;
	USBH_URBStateTypeDef usbhUrbState = USBH_URB_IDLE;

	if (writingState->written) {
		usbhUrbState = USBH_LL_GetURBState(phost, handle->outPipeNo);

		switch (usbhUrbState) {
		case USBH_URB_DONE:
		{
			mlog("USB host finished writing");

			if (handle->cmd.in == NULL) {
				DX_ActiveServoClass_Rsp_TypeDef rsp;

				rsp.status = DX__ACTIVE_SERVO_CLASS__OK;
				osMessageQueuePut(handle->rspMsgQueueId, &rsp, 0U, 0U);

				handle->nextState = DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__IDLE;
			} else {
				handle->nextState = DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__READING;
			}

			break;
		}
		case USBH_URB_NOTREADY:
		{
			mlog("USB host was not ready to write, rewriting");

			writingState->written = false; // Write again.

			break;
		}
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
		printf("Stuff: %02x\r\n", handle->cmd.out[0]);

		usbhStatus = USBH_BulkSendData(phost, handle->cmd.out,
				64, handle->outPipeNo, 1U);

		mlog("Writing bulk data");

		if (usbhStatus != USBH_OK) {
			mlog("Failed to send bulk data, USB host status: %d", usbhStatus);

			usbhStatus = USBH_FAIL;
		}

		writingState->written = true;
	}

	return usbhStatus;
}

USBH_StatusTypeDef DX_USB_ActiveServoClass_WritingState_Exit(USBH_HandleTypeDef *phost)
{
	USBH_StatusTypeDef status = USBH_OK;

	mlog("Exiting writing state");

	return status;
}
