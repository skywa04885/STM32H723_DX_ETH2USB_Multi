/*
 * reading.h
 *
 *  Created on: Sep 14, 2023
 *      Author: luke
 */

#ifndef INC_DX_ETH2USB_ACTIVE_SERVO_CLASS_STATES_READING_H_
#define INC_DX_ETH2USB_ACTIVE_SERVO_CLASS_STATES_READING_H_

#include <usbh_core.h>

USBH_StatusTypeDef DX_USB_ActiveServoClass_ReadingState_Entry(USBH_HandleTypeDef *phost);

USBH_StatusTypeDef DX_USB_ActiveServoClass_ReadingState_Do(USBH_HandleTypeDef *phost);

USBH_StatusTypeDef DX_USB_ActiveServoClass_ReadingState_Exit(USBH_HandleTypeDef *phost);

#endif /* INC_DX_ETH2USB_ACTIVE_SERVO_CLASS_STATES_READING_H_ */
