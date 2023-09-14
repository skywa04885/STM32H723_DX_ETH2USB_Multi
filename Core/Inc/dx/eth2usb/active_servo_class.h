/*
 * active_servo_class.h
 *
 *  Created on: Sep 11, 2023
 *      Author: luke
 */

#ifndef INC_DX_ETH2USB_ACTIVE_SERVO_CLASS_H_
#define INC_DX_ETH2USB_ACTIVE_SERVO_CLASS_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <cmsis_os.h>
#include <usbh_core.h>

typedef struct {
	bool written;
} DX_ActiveServoClass_WritingState_t;

typedef struct {
	bool reading;
} DX_ActiveServoClass_ReadingState_t;

typedef struct {
	uint8_t *out;
	uint8_t *in;
} DX_ActiveServoClass_Cmd_TypeDef;

typedef enum {
	DX__ACTIVE_SERVO_CLASS__OK = 0, DX__ACTIVE_SERVO_CLASS__ERR,
} DX_ActiveServoClass_StatusTypeDef;

typedef struct {
	DX_ActiveServoClass_StatusTypeDef status;
} DX_ActiveServoClass_Rsp_TypeDef;

typedef enum {
	DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__IDLE = 0,
	DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__WRITING,
	DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__READING,
	DX__ETH2USB__ACTIVE_SERVO_CLASS_STATE__ERROR,
} DX_ActiveServoClass_State_TypeDef;

typedef struct {
	// End-point addresses.
	uint8_t inEpAddr;
	uint8_t outEpAddr;
	// End-point max packet sizes.
	uint16_t inEpMaxPktSize;
	uint16_t outEpMaxPktSize;
	// Pipe numbers.
	uint8_t inPipeNo;
	uint8_t outPipeNo;
	// Mutexes.
	osMutexId_t availabilityMutexId;
	// Message queues.
	osMessageQueueId_t cmdMsgQueueId;
	osMessageQueueId_t rspMsgQueueId;
	// State.
	bool started;
	DX_ActiveServoClass_State_TypeDef state;
	DX_ActiveServoClass_State_TypeDef nextState;
	DX_ActiveServoClass_Cmd_TypeDef cmd;
	// States.
	DX_ActiveServoClass_WritingState_t writingState;
	DX_ActiveServoClass_ReadingState_t readingState;
} DX_ActiveServoClass_HandleTypeDef;

DX_ActiveServoClass_StatusTypeDef DX_ActiveServoClass_Cmd(
		USBH_HandleTypeDef *phost, uint8_t *out, uint8_t *in);

extern USBH_ClassTypeDef gDxActiveServoClass;
#define DX_ACTIVE_SERVO_CLASS &gDxActiveServoClass

#endif /* INC_DX_ETH2USB_ACTIVE_SERVO_CLASS_H_ */
