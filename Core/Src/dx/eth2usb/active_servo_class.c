/*
 * active_servo_class.c
 *
 *  Created on: Sep 11, 2023
 *      Author: luke
 */

#include "dx/eth2usb/active_servo_class.h"

USBH_ClassTypeDef DX_USB_ACTIVE_SERVO_CLASS = {
		.Name = "Active Servo",
		.ClassCode = DX_USB_ACTIVE_SERVO_CLASS_CODE,
		.Data = NULL,
};

static void DX_USB_ActiveServoClass_Init(USBH_HandleTypeDef *phost)
{

}
