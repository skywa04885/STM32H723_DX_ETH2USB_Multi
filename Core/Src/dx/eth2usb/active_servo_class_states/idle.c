/*
 * idle.c
 *
 *  Created on: Sep 14, 2023
 *      Author: luke
 */

#include "dx/eth2usb/active_servo_class.h"
#include "dx/eth2usb/active_servo_class_states/idle.h"
#include "logging.h"

USBH_StatusTypeDef DX_USB_ActiveServoClass_IdleState_Entry(USBH_HandleTypeDef *phost)
{
	USBH_StatusTypeDef status = USBH_OK;

	mlog("Entering idle state");

	return status;
}

USBH_StatusTypeDef DX_USB_ActiveServoClass_IdleState_Do(USBH_HandleTypeDef *phost)
{
	DX_ActiveServoClass_HandleTypeDef *handle =
			(DX_ActiveServoClass_HandleTypeDef*) phost->pActiveClass->pData;
	USBH_StatusTypeDef usbhStatus = USBH_OK;
	osStatus_t osStatus = osOK;

	osStatus = osMessageQueueGet(handle->cmdMsgQueueId, &handle->cmd, 0U, 0U);

	if (osStatus != osOK) {
		if (osStatus == osErrorResource) {
			usbhStatus = USBH_OK;

			return usbhStatus;
		}

		mlog("Failed to get command from the command message queue, status: %d", osStatus);
		return USBH_FAIL;
	}

	mlog("Received message, starting writing");

	handle->nextState = DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__WRITING;

	return usbhStatus;
}

USBH_StatusTypeDef DX_USB_ActiveServoClass_IdleState_Exit(USBH_HandleTypeDef *phost)
{
	USBH_StatusTypeDef status = USBH_OK;

	mlog("Exiting idle state");

	return status;
}
