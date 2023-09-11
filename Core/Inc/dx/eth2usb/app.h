/*
 * app.h
 *
 *  Created on: Sep 11, 2023
 *      Author: luke
 */

#ifndef INC_DX_ETH2USB_APP_H_
#define INC_DX_ETH2USB_APP_H_

#include <assert.h>
#include <stdbool.h>
#include <cmsis_os.h>
#include <sys/socket.h>
#include "dx/eth2usb/eth_frame.h"

#ifndef DX_ETH2USB_APP_ETH_FRAME_MEM_POOL_SIZE
#define DX_ETH2USB_APP_ETH_FRAME_MEM_POOL_SIZE 20
#endif

#ifndef DX_ETH2USB_APP_ETH2USB_MSG_QUEUE_SIZE
#define DX_ETH2USB_APP_ETH2USB_MSG_QUEUE_SIZE 10
#endif

#ifndef DX_ETH2USB_APP_USB2ETH_MSG_QUEUE_SIZE
#define DX_ETH2USB_APP_USB2ETH_MSG_QUEUE_SIZE 10
#endif

typedef struct {
	int32_t fd;
	struct sockaddr_in addr;
} DX_ETH2USB_App_EthThread_ServerState_t;

typedef struct {
	int32_t fd;
	struct sockaddr_in addr;
	socklen_t socklen;
} DX_ETH2USB_App_EthThread_ClientState_t;

typedef struct {
	// Sockets.
	DX_ETH2USB_App_EthThread_ServerState_t server;
	DX_ETH2USB_App_EthThread_ClientState_t client;
	// Frames.
	DX_ETH2USB_ETHFrame_t *incommingFrame;
	DX_ETH2USB_ETHFrame_t *outgoingFrame;
	// Frame writing.
	uint32_t nBytesWritten;
	uint32_t nBytesRead;
} DX_ETH2USB_App_EthThreadState_t;

typedef struct {
	uint32_t lastEthStatusBlinkTick;
	bool wasEthConnected;
	uint32_t lastUsbStatusBlinkTick;
	bool wasUsbConnected;
} DX_ETH2USB_App_StatusThreadState_t;

typedef struct {
	DX_ETH2USB_ETHFrame_t *frame;
} DX_ETH2USB_App_UsbThreadState_t;

typedef struct {
	// Memory pool identifiers.
	osMemoryPoolId_t ethFrameMemPoolId;
	// Message queues.
	osMessageQueueId_t eth2usbMsgQueueId;
	osMessageQueueId_t usb2ethMsgQueueId;
	// Thread attributes.
	osThreadAttr_t ethThreadAttr;
	osThreadAttr_t usbThreadAttr;
	osThreadAttr_t statusThreadAttr;
	// Thread identifiers.
	osThreadId_t ethThreadId;
	osThreadId_t usbThreadId;
	osThreadId_t statusThreadId;
	// Thread states.
	DX_ETH2USB_App_EthThreadState_t ethThreadState;
	DX_ETH2USB_App_StatusThreadState_t statusThreadState;
	DX_ETH2USB_App_UsbThreadState_t usbThreadState;
} DX_ETH2USB_AppState_t;

/**
 * Initializes the application.
 */
void DX_ETH2USB_App_Init(DX_ETH2USB_AppState_t *app);

/**
 * Starts the application.
 */
void DX_ETH2USB_App_Start(DX_ETH2USB_AppState_t *app);

#endif /* INC_DX_ETH2USB_APP_H_ */
