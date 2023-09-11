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

/// Checks whether the connected device is valid.
static bool USBH_UserProcess_HandleConnect_IsDeviceValid(
		USBH_HandleTypeDef *phost) {
	const USBH_DeviceTypeDef *device = &phost->device;
	const USBH_DevDescTypeDef *deviceDescription = &device->DevDesc;

	if (deviceDescription->idVendor == DX_ETH2USB__USB_DEVICE__VENDOR_ID) {
		mlog("Vendor ID mismatch got %04X expected %04X",
				deviceDescription->idVendor, DX_ETH2USB__USB_DEVICE__VENDOR_ID);

		return false;
	}

	if (deviceDescription->idProduct == DX_ETH2USB__USB_DEVICE__PRODUCT_ID) {
		mlog("Product ID mismatch got %04X expected %04X",
				deviceDescription->idProduct,
				DX_ETH2USB__USB_DEVICE__PRODUCT_ID);

		return false;
	}

	return true;
}

/// Prints the info of the device that got connected.
static void USBH_UserProcess_SelectConfig_PrintInfo(USBH_HandleTypeDef *phost) {
	const USBH_DeviceTypeDef *device = &phost->device;
	const USBH_DevDescTypeDef *deviceDescription = &device->DevDesc;
	USBH_StatusTypeDef status = USBH_OK;
	char buffer[64];

	mlog("Connected USB-Device:");

	memset(buffer, 0, sizeof(buffer));
	while ((status = USBH_Get_StringDesc(phost,
			deviceDescription->iManufacturer, (uint8_t*) buffer, sizeof(buffer)))
			!= USBH_BUSY)
		continue;

	if (status != USBH_OK)
		Error_Handler();

	mlog("\t-Manufacturer: %s", buffer);

	memset(buffer, 0, sizeof(buffer));
	while ((status = USBH_Get_StringDesc(phost, deviceDescription->iProduct,
			(uint8_t*) buffer, sizeof(buffer))) != USBH_BUSY)
		continue;

	if (status != USBH_OK)
		Error_Handler();

	mlog("\t-Product: %s", buffer);

	memset(buffer, 0, sizeof(buffer));
	while ((status = USBH_Get_StringDesc(phost,
			deviceDescription->iSerialNumber, (uint8_t*) buffer, sizeof(buffer)))
			!= USBH_BUSY)
		continue;

	if (status != USBH_OK)
		Error_Handler();

	mlog("\t-Serial-number: %s", buffer);
}

/// Opens all the pipes to the USB device.
static void USBH_UserProcess_HandleConnect_OpenPipes(USBH_HandleTypeDef *phost) {
	USBH_StatusTypeDef status = USBH_OK;
	const USBH_DeviceTypeDef *device = &phost->device;

	while ((status = USBH_OpenPipe(phost,
	DX_ETH2USB__USB_DEVICE__PRIMARY_PIPE_NO,
	DX_ETH2USB__USB_DEVICE__PRIMARY_ENDPOINT_NO, device->address,
			phost->device.speed, EP_TYPE_BULK,
			DX_ETH2USB__USB_DEVICE__MAX_PACKET_SIZE)) != USBH_BUSY)
		continue;

	if (status != USBH_OK) {
		mlog("Failed to open pipe");
		Error_Handler();
	}
}

/// Closes all the pipes.
static void USBH_UserProcess_HandleClose_ClosePipes(USBH_HandleTypeDef *phost) {
	USBH_StatusTypeDef status = USBH_OK;

	while ((status = USBH_ClosePipe(phost,
	DX_ETH2USB__USB_DEVICE__PRIMARY_PIPE_NO)) == USBH_BUSY)
		continue;

	if (status != USBH_OK) {
		mlog("Failed to close pipe");
		Error_Handler();
	}
}

/// Handles the moment when an USB device gets disconnected.
static void USBH_UserProcess_HandleDisconnect(USBH_HandleTypeDef *phost) {
	mlog("USB Device got disconnected");

	if (!DX_USBH_IsDeviceConnected)
		return;

	USBH_UserProcess_HandleClose_ClosePipes(phost);

	DX_USBH_IsDeviceConnected = false;
}

/// Gets called once a new USB device has connected.
static void USBH_UserProcess_HandleConnect(USBH_HandleTypeDef *phost) {
	mlog("USB Device got connected");

	if (DX_USBH_IsDeviceConnected)
		return;

#ifdef DX_ETH2USB__USB_DEVICE__VALIDATE_VENDOR_ID_AND_PRODUCT_ID
	if (!USBH_UserProcess_HandleConnect_IsDeviceValid(phost))
		return;
#endif

	USBH_UserProcess_HandleConnect_OpenPipes(phost);

	DX_USBH_IsDeviceConnected = true;

}
/* USER CODE END 1 */

/**
 * Init USB host library, add supported class and start the library
 * @retval None
 */
void MX_USB_HOST_Init(void) {
	/* USER CODE BEGIN USB_HOST_Init_PreTreatment */

	/* USER CODE END USB_HOST_Init_PreTreatment */

	/* Init host Library, add supported class and start the library. */
	if (USBH_Init(&hUsbHostHS, USBH_UserProcess, HOST_HS) != USBH_OK) {
		Error_Handler();
	}
	if (USBH_RegisterClass(&hUsbHostHS, USBH_AUDIO_CLASS) != USBH_OK) {
		Error_Handler();
	}
	if (USBH_RegisterClass(&hUsbHostHS, USBH_CDC_CLASS) != USBH_OK) {
		Error_Handler();
	}
	if (USBH_RegisterClass(&hUsbHostHS, USBH_MSC_CLASS) != USBH_OK) {
		Error_Handler();
	}
	if (USBH_RegisterClass(&hUsbHostHS, USBH_HID_CLASS) != USBH_OK) {
		Error_Handler();
	}
	if (USBH_RegisterClass(&hUsbHostHS, USBH_MTP_CLASS) != USBH_OK) {
		Error_Handler();
	}
	if (USBH_Start(&hUsbHostHS) != USBH_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN USB_HOST_Init_PostTreatment */

	/* USER CODE END USB_HOST_Init_PostTreatment */
}

/*
 * user callback definition
 */
static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id) {
	/* USER CODE BEGIN CALL_BACK_1 */

	switch (id) {
	case HOST_USER_SELECT_CONFIGURATION:

		// For some reason it crashes if we don't do it in the select configuration.
		USBH_UserProcess_SelectConfig_PrintInfo(phost);

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

