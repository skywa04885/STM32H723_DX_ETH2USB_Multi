/*
 * app.c
 *
 *  Created on: Sep 11, 2023
 *      Author: luke
 */

#include <string.h>
#include <usbh_core.h>

#include "dx/eth2usb/app.h"
#include "logging.h"
#include "settings.h"
#include "main.h"

// TODO: Figure out why read() can return -1 with errno 0. For now this weird behavior
//  won't cause trouble, since my code will ignore errno 0 (dirty hack!).

extern USBH_HandleTypeDef hUsbHostHS;
extern bool DX_USBH_IsDeviceConnected;

void DX_ETH2USB_App_Init_CreateMemPools(DX_ETH2USB_AppState_t *app) {
	app->ethFrameMemPoolId = osMemoryPoolNew(
	DX_ETH2USB_APP_ETH_FRAME_MEM_POOL_SIZE, sizeof(DX_ETH2USB_ETHFrame_t),
	NULL);
	if (app->ethFrameMemPoolId == NULL)
		Error_Handler();
}

void DX_ETH2USB_App_Init_CreateMsgQueues(DX_ETH2USB_AppState_t *app) {
	app->eth2usbMsgQueueId = osMessageQueueNew(
	DX_ETH2USB_APP_ETH2USB_MSG_QUEUE_SIZE, sizeof(DX_ETH2USB_ETHFrame_t*),
	NULL);
	if (app->eth2usbMsgQueueId == NULL)
		Error_Handler();

	app->usb2ethMsgQueueId = osMessageQueueNew(
	DX_ETH2USB_APP_USB2ETH_MSG_QUEUE_SIZE, sizeof(DX_ETH2USB_ETHFrame_t*),
	NULL);
	if (app->usb2ethMsgQueueId == NULL)
		Error_Handler();
}

static void DX_ETH2USB_App_Init_ThreadAttrs(DX_ETH2USB_AppState_t *app) {
	app->ethThreadAttr.name = "DX_ETH2USB_App_EthThread";
	app->ethThreadAttr.stack_size = 1024;
	app->ethThreadAttr.priority = osPriorityNormal;

	app->ethThreadAttr.name = "DX_ETH2USB_App_UsbThread";
	app->ethThreadAttr.stack_size = 2048;
	app->ethThreadAttr.priority = osPriorityNormal;

	app->statusThreadAttr.name = "DX_ETH2USB_App_StatusThread";
	app->statusThreadAttr.stack_size = 256;
	app->statusThreadAttr.priority = osPriorityNormal;
}

static void DX_ETH2USB_App_Init_Threads(DX_ETH2USB_AppState_t *app) {
	app->ethThreadId = NULL;
	app->usbThreadId = NULL;
	app->statusThreadId = NULL;
}

static void DX_ETH2USB_App_Init_ThreadStates_EthThread_Server(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *ethThreadState = &app->ethThreadState;
	DX_ETH2USB_App_EthThread_ServerState_t *server = &ethThreadState->server;

	server->fd = -1;

	memset(&server->addr, 0, sizeof(struct sockaddr_in));
	server->addr.sin_family = AF_INET;
	server->addr.sin_port = htons(8000);
	server->addr.sin_addr.s_addr = inet_addr("0.0.0.0");
}

static void DX_ETH2USB_App_Init_ThreadStates_EthThread_Client(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *ethThreadState = &app->ethThreadState;
	DX_ETH2USB_App_EthThread_ClientState_t *client = &ethThreadState->client;

	client->fd = -1;
	client->connected = false;

	memset(&client->addr, 0, sizeof(struct sockaddr_in));
}

static void DX_ETH2USB_App_Init_ThreadStates_EthThread(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *ethThreadState = &app->ethThreadState;

	DX_ETH2USB_App_Init_ThreadStates_EthThread_Server(app);
	DX_ETH2USB_App_Init_ThreadStates_EthThread_Client(app);

	ethThreadState->incommingFrame = NULL;
	ethThreadState->outgoingFrame = NULL;
}

static void DX_ETH2USB_App_Init_ThreadStates_StatusThread(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_StatusThreadState_t *statusThreadState =
			&app->statusThreadState;

	statusThreadState->lastEthStatusBlinkTick = 0;
	statusThreadState->lastUsbStatusBlinkTick = 0;

	statusThreadState->wasEthConnected = false;
	statusThreadState->wasUsbConnected = false;
}

static void DX_ETH2USB_App_Init_ThreadStates(DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_Init_ThreadStates_EthThread(app);
	DX_ETH2USB_App_Init_ThreadStates_StatusThread(app);
}

void DX_ETH2USB_App_Init(DX_ETH2USB_AppState_t *app) {
	mlog("Initializing app");

	DX_ETH2USB_App_Init_CreateMemPools(app);
	DX_ETH2USB_App_Init_CreateMsgQueues(app);
	DX_ETH2USB_App_Init_ThreadAttrs(app);
	DX_ETH2USB_App_Init_Threads(app);
	DX_ETH2USB_App_Init_ThreadStates(app);
}

static void DX_ETH2USB_App_EthThread_InitializeWritingOfFrame(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	uint32_t ethFrameCount = 0;
	osStatus_t status = osOK;

	ethFrameCount = osMessageQueueGetCount(app->usb2ethMsgQueueId);
	if (ethFrameCount == 0)
		return;

	status = osMessageQueueGet(app->usb2ethMsgQueueId,
			&threadState->outgoingFrame, NULL, osWaitForever);
	if (status != osOK)
		Error_Handler();

	threadState->nBytesWritten = 0U;
}

static void DX_ETH2USB_App_EthThread_CloseClientSocket(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	DX_ETH2USB_App_EthThread_ClientState_t *client = &threadState->client;
	int32_t ret = -1;

	ret = close(client->fd);

	if (ret == -1) {
		mlog("Failed to close client socket, error (%d): %s", errno,
				strerror(errno));
		Error_Handler();
	}

	client->fd = -1;
	client->connected = false;
}

static void DX_ETH2USB_App_EthThread_WriteOutgoingFrame_HandleSuccess(
		DX_ETH2USB_AppState_t *app, int32_t ret) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	osStatus status = osOK;

	threadState->nBytesWritten += (uint32_t) ret;

	mlog("Wrote %u out of %u bytes", threadState->nBytesWritten,
			threadState->outgoingFrame->payloadSize + sizeof(uint32_t));

	if (threadState->nBytesWritten
			< threadState->outgoingFrame->payloadSize + sizeof(uint32_t))
		return;

	status = osMemoryPoolFree(app->ethFrameMemPoolId,
			threadState->outgoingFrame);
	if (status != osOK)
		Error_Handler();

	threadState->outgoingFrame = NULL;
}

static void DX_ETH2USB_App_EthThread_WriteOutgoingFrame_EndOfStream(
		DX_ETH2USB_AppState_t *app) {
	mlog("Received EOS while writing frame");

	DX_ETH2USB_App_EthThread_CloseClientSocket(app);
}

static void DX_ETH2USB_App_EthThread_WriteOutgoingFrame_HandleError(
		DX_ETH2USB_AppState_t *app) {
	if (errno == EAGAIN || errno == 0)
		return;
	else if (errno == ECONNRESET) {
		mlog("Connection got closed while writing frame");
		DX_ETH2USB_App_EthThread_CloseClientSocket(app);
	} else {
		mlog("Failed to write frame, error (%d): %s", errno, strerror(errno));
		Error_Handler();
	}
}

static void DX_ETH2USB_App_EthThread_WriteOutgoingFrame(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	DX_ETH2USB_App_EthThread_ClientState_t *client = &threadState->client;
	int32_t ret = -1;

	const uint8_t *bytes =
			&((uint8_t*) threadState->outgoingFrame)[threadState->nBytesWritten];
	const uint32_t bytesToWrite = threadState->outgoingFrame->payloadSize
			+ sizeof(uint32_t) - threadState->nBytesWritten;

	ret = write(client->fd, bytes, bytesToWrite);

	if (ret > 0) {
		DX_ETH2USB_App_EthThread_WriteOutgoingFrame_HandleSuccess(app, ret);
	} else if (ret == 0) {
		DX_ETH2USB_App_EthThread_WriteOutgoingFrame_EndOfStream(app);
	} else if (ret == -1) {
		DX_ETH2USB_App_EthThread_WriteOutgoingFrame_HandleError(app);
	}
}

static void DX_ETH2USB_App_EthThread_ReadIncommingFrame_HandleSuccess_ForwardFrameToUSB(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	osStatus_t status = osOK;

	status = osMessageQueuePut(app->eth2usbMsgQueueId,
			&threadState->incommingFrame, 0U, osWaitForever);
	if (status != osOK) {
		Error_Handler();
	}

	threadState->incommingFrame = NULL;
}

static void DX_ETH2USB_App_EthThread_ReadIncommingFrame_HandleSuccess(
		DX_ETH2USB_AppState_t *app, uint32_t nBytesToRead, int32_t ret) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	DX_ETH2USB_ETHFrame_t *frame = threadState->incommingFrame;

	threadState->nBytesRead += (uint32_t) ret;

	mlog("Read %u bytes out of the %u bytes", ret, nBytesToRead);

	if (threadState->nBytesRead == 4) {
		if (frame->payloadSize == 0) {
			mlog("Got payload of size 0, this is not allowed.");
			DX_ETH2USB_App_EthThread_CloseClientSocket(app);
		} else if (frame->payloadSize > DX_ETH2USB_ETHFRAME_MAX_PAYLOAD_SIZE) {
			mlog(
					"Got payload of size %u which exceeds the max payload size of %u",
					frame->payloadSize, DX_ETH2USB_ETHFRAME_MAX_PAYLOAD_SIZE);
			DX_ETH2USB_App_EthThread_CloseClientSocket(app);
		}
	} else if (threadState->nBytesRead > 4
			&& threadState->nBytesRead
					>= frame->payloadSize + sizeof(uint32_t)) {
		mlog("Received entire frame of size %u", threadState->nBytesRead);
		DX_ETH2USB_App_EthThread_ReadIncommingFrame_HandleSuccess_ForwardFrameToUSB(
				app);
	}
}

static void DX_ETH2USB_App_EthThread_ReadIncommingFrame_HandleEndOfStream(
		DX_ETH2USB_AppState_t *app) {
	mlog("Received end of stream while reading incomming frame");
	DX_ETH2USB_App_EthThread_CloseClientSocket(app);
}

static void DX_ETH2USB_App_EthThread_ReadIncommingFrame_HandleError(
		DX_ETH2USB_AppState_t *app) {

	if (errno == EAGAIN || errno == 0)
		return;
	else if (errno == ECONNRESET) {
		mlog("Remote closed stream while reading incomming frame");
		DX_ETH2USB_App_EthThread_CloseClientSocket(app);
	} else {
		mlog("Failed to read incoming frame, error (%d): %s", errno,
				strerror(errno));
		Error_Handler();
	}
}

static void DX_ETH2USB_App_EthThread_StartReadingIncommingFrame(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;

	if (osMemoryPoolGetSpace(app->ethFrameMemPoolId) == 0)
		return;

	threadState->incommingFrame = osMemoryPoolAlloc(app->ethFrameMemPoolId, 0U);
	if (threadState->incommingFrame == NULL)
		Error_Handler();

	threadState->nBytesRead = 0;
}

static void DX_ETH2USB_App_EthThread_ReadIncommingFrame(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	DX_ETH2USB_App_EthThread_ClientState_t *client = &threadState->client;
	DX_ETH2USB_ETHFrame_t *frame = threadState->incommingFrame;
	uint32_t nBytesToRead = 0;
	int32_t ret = -1;

	if (threadState->nBytesRead >= sizeof(uint32_t)) {
		nBytesToRead = frame->payloadSize + sizeof(uint32_t)
				- threadState->nBytesRead;
	} else {
		nBytesToRead = sizeof(uint32_t) - threadState->nBytesRead;
	}

	uint8_t *bytes = &((uint8_t*) frame)[threadState->nBytesRead];

	ret = read(client->fd, bytes, nBytesToRead);

	if (ret > 0) {
		DX_ETH2USB_App_EthThread_ReadIncommingFrame_HandleSuccess(app, nBytesToRead, ret);
	} else if (ret == 0) {
		DX_ETH2USB_App_EthThread_ReadIncommingFrame_HandleEndOfStream(app);
	} else if (ret == -1) {
		DX_ETH2USB_App_EthThread_ReadIncommingFrame_HandleError(app);
	}
}

/// Starts the server socket for the Ethernet thread.
static void DX_ETH2USB_App_EthThread_StartServerSocket(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	DX_ETH2USB_App_EthThread_ServerState_t *server = &threadState->server;

	int32_t ret = -1;

	server->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server->fd == -1) {
		mlog("Failed to create server socket, error (%d): %s", errno,
				strerror(errno));
		Error_Handler();
	}

	ret = bind(server->fd, (struct sockaddr* )&server->addr,
			sizeof(struct sockaddr_in));
	if (ret == -1) {
		mlog("Failed to bind server socket, error (%d): %s", errno,
				strerror(errno));
		Error_Handler();
	}

	ret = listen(server->fd, 0);
	if (ret == -1) {
		mlog("Failed to listen server socket, error (%d): %s", errno,
				strerror(errno));
		Error_Handler();
	}
}

static void DX_ETH2USB_App_EthThread_AcceptClientSocket(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	DX_ETH2USB_App_EthThread_ServerState_t *server = &threadState->server;
	DX_ETH2USB_App_EthThread_ClientState_t *client = &threadState->client;
	int32_t flags = 0;
	int32_t ret = 0;

	client->fd = accept(server->fd, (struct sockaddr* ) &client->addr,
			&client->socklen);
	if (client->fd == -1) {
		mlog("Failed to accept client socket, error (%d): %s", errno,
				strerror(errno));
		Error_Handler();
	}

	flags = fcntl(client->fd, F_GETFL, 0);
	if (ret == -1) {
		mlog("Failed to get client socket flags, error (%d): %s", errno,
				strerror(errno));
		Error_Handler();
	}

	flags |= O_NONBLOCK;

	ret = fcntl(client->fd, F_SETFL, flags);
	if (ret == -1) {
		mlog("Failed to set client socket flags, error (%d): %s", errno,
				strerror(errno));
		Error_Handler();
	}

	mlog("Accepted client socket %s:%u", inet_ntoa(client->addr),
			ntohs(client->addr.sin_port));

	threadState->nBytesRead = 0U;

	client->connected = true;
}

static void DX_ETH2USB_App_EthThread(void *arg) {
	DX_ETH2USB_AppState_t *app = arg;
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	DX_ETH2USB_App_EthThread_ClientState_t *client = &threadState->client;

	DX_ETH2USB_App_EthThread_StartServerSocket(app);

	while (true) {
		while (threadState->client.fd == -1)
			DX_ETH2USB_App_EthThread_AcceptClientSocket(app);

		if (threadState->outgoingFrame == NULL)
			DX_ETH2USB_App_EthThread_InitializeWritingOfFrame(app);

		if (threadState->outgoingFrame != NULL)
			DX_ETH2USB_App_EthThread_WriteOutgoingFrame(app);

		if (client->fd == -1)
			continue;

		if (threadState->incommingFrame == NULL)
			DX_ETH2USB_App_EthThread_StartReadingIncommingFrame(app);
		if (threadState->incommingFrame != NULL)
			DX_ETH2USB_App_EthThread_ReadIncommingFrame(app);

		osDelay(1);
	}
}

/// Gets a single frame that the USB thread should forward to the USB device.
static void DX_ETH2USB_App_UsbThread_GetEthFrame(DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_UsbThreadState_t *threadState = &app->usbThreadState;
	osStatus_t status = osOK;

	status = osMessageQueueGet(app->eth2usbMsgQueueId, &threadState->frame,
			NULL, osWaitForever);
	if (status != osOK)
		Error_Handler();
}

/// Puts a single frame on the message queue for the ETH thread to forward to
/// the ETH client.
static void DX_ETH2USB_App_UsbThread_PutEthFrame(DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_UsbThreadState_t *threadState = &app->usbThreadState;
	osStatus status = osOK;

	status = osMessageQueuePut(app->usb2ethMsgQueueId, &threadState->frame, 0U,
			osWaitForever);
	if (status != osOK)
		Error_Handler();

}

/// The thread that forwards frames to the USB device.
static void DX_ETH2USB_App_UsbThread(void *arg) {
	DX_ETH2USB_AppState_t *app = arg;
	DX_ETH2USB_App_UsbThreadState_t *threadState = &app->usbThreadState;

	while (true) {
		while (!DX_USBH_IsDeviceConnected)
			osDelay(50);

		DX_ETH2USB_App_UsbThread_GetEthFrame(app);
		mlog("Got new Ethernet frame with payload"
				"of size %u to transmit to USB",
				threadState->frame->payloadSize);

		DX_ETH2USB_App_UsbThread_PutEthFrame(app);

		osDelay(1);
	}
}

/// Updates the Ethernet status LED.
static void DX_ETH2USB_App_StatusThread_UpdateEthStatus(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_StatusThreadState_t *state = &app->statusThreadState;
	uint32_t currentTick = HAL_GetTick();

	const bool isEthConnected = app->ethThreadState.client.connected;

	if (!isEthConnected && state->wasEthConnected) {
		HAL_GPIO_WritePin(LED_YELLOW_GPIO_Port, LED_YELLOW_Pin, GPIO_PIN_RESET);

		state->lastEthStatusBlinkTick = currentTick;
	} else if (!isEthConnected && !state->wasEthConnected) {
		if (currentTick
				- state->lastEthStatusBlinkTick> DX_ETH2USB__STATUS__ETHERNET_BLINK_INTERVAL) {
			HAL_GPIO_TogglePin(LED_YELLOW_GPIO_Port, LED_YELLOW_Pin);

			state->lastEthStatusBlinkTick = currentTick;
		}
	} else if (isEthConnected && !state->wasEthConnected) {
		HAL_GPIO_WritePin(LED_YELLOW_GPIO_Port, LED_YELLOW_Pin, GPIO_PIN_SET);
	}

	state->wasEthConnected = isEthConnected;
}

/// Updates the USB status LED.
static void DX_ETH2USB_App_StatusThread_UpdateUsbStatus(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_StatusThreadState_t *state = &app->statusThreadState;
	uint32_t currentTick = HAL_GetTick();

	const bool isUsbConnected = DX_USBH_IsDeviceConnected;

	if (!isUsbConnected && state->wasUsbConnected) {
		HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);

		state->lastUsbStatusBlinkTick = currentTick;
	} else if (!isUsbConnected && !state->wasUsbConnected) {
		if (currentTick
				- state->lastUsbStatusBlinkTick> DX_ETH2USB__STATUS__USB_BLINK_INTERVAL) {
			HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);

			state->lastUsbStatusBlinkTick = currentTick;
		}
	} else if (isUsbConnected && !state->wasUsbConnected) {
		HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
	}

	state->wasUsbConnected = isUsbConnected;
}

static void DX_ETH2USB_App_StatusThread(void *arg) {
	DX_ETH2USB_AppState_t *app = arg;

	while (true) {
		DX_ETH2USB_App_StatusThread_UpdateEthStatus(app);
		DX_ETH2USB_App_StatusThread_UpdateUsbStatus(app);

		osDelay(50);
	}
}

void DX_ETH2USB_App_Start(DX_ETH2USB_AppState_t *app) {
	mlog("Starting app");

	app->ethThreadId = osThreadNew(DX_ETH2USB_App_EthThread, app,
			&app->ethThreadAttr);
	if (app->ethThreadId == NULL)
		Error_Handler();

	app->usbThreadId = osThreadNew(DX_ETH2USB_App_UsbThread, app,
			&app->usbThreadAttr);
	if (app->usbThreadId == NULL)
		Error_Handler();

	app->statusThreadId = osThreadNew(DX_ETH2USB_App_StatusThread, app,
			&app->statusThreadAttr);
	if (app->statusThreadId == NULL)
		Error_Handler();
}
