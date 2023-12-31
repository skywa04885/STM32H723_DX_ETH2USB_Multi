/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file            : usb_host.c
 * @version         : v1.0_Cube
 * @brief           : This file implements the USB Host
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "usb_host.h"
#include "usbh_core.h"
#include "usbh_audio.h"
#include "usbh_cdc.h"
#include "usbh_msc.h"
#include "usbh_hid.h"
#include "usbh_mtp.h"

/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include "settings.h"
#include "logging.h"
#include "dx/eth2usb/active_servo_class.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
bool DX_USBH_IsDeviceConnected = false;
/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USB Host core handle declaration */
USBH_HandleTypeDef hUsbHostHS;
ApplicationTypeDef Appli_state = APPLICATION_IDLE;

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/*
 * user callback declaration
 */
static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id);

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */

/// Handles the moment when an USB device gets disconnected.
static void USBH_UserProcess_HandleDisconnect(USBH_HandleTypeDef *phost) {
	mlog("USB Device got disconnected");

	if (!DX_USBH_IsDeviceConnected)
		return;

//	USBH_UserProcess_HandleClose_ClosePipes(phost);

	DX_USBH_IsDeviceConnected = false;
}

/// Gets called once a new USB device has connected.
static void USBH_UserProcess_HandleConnect(USBH_HandleTypeDef *phost) {
	mlog("USB Device got connected");

	if (DX_USBH_IsDeviceConnected)
		return;

	DX_USBH_IsDeviceConnected = true;

}
/* USER CODE END 1 */

/**
  * Init USB host library, add supported class and start the library
  * @retval None
  */
void MX_USB_HOST_Init(void)
{
  /* USER CODE BEGIN USB_HOST_Init_PreTreatment */
  /* USER CODE END USB_HOST_Init_PreTreatment */

  /* Init host Library, add supported class and start the library. */
  if (USBH_Init(&hUsbHostHS, USBH_UserProcess, HOST_HS) != USBH_OK)
  {
    Error_Handler();
  }
  if (USBH_RegisterClass(&hUsbHostHS, DX_ACTIVE_SERVO_CLASS) != USBH_OK) {
	  Error_Handler();
  }
  if (USBH_Start(&hUsbHostHS) != USBH_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_HOST_Init_PostTreatment */
  /* USER CODE END USB_HOST_Init_PostTreatment */
}

/*
 * user callback definition
 */
static void USBH_UserProcess  (USBH_HandleTypeDef *phost, uint8_t id)
{
  /* USER CODE BEGIN CALL_BACK_1 */

	switch (id) {
	case HOST_USER_SELECT_CONFIGURATION:

		break;

	case HOST_USER_DISCONNECTION:
		Appli_state = APPLICATION_DISCONNECT;

		USBH_UserProcess_HandleDisconnect(phost);

		break;

	case HOST_USER_CLASS_ACTIVE:
		Appli_state = APPLICATION_READY;

		break;

	case HOST_USER_CONNECTION:

		Appli_state = APPLICATION_START;

		USBH_UserProcess_HandleConnect(phost);

		break;

	default:
		break;
	}
  /* USER CODE END CALL_BACK_1 */
}

/**
  * @}
  */

/**
  * @}
  */

