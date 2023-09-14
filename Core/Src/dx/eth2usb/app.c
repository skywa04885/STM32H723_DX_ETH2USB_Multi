/*
 * app.c
 *
 *  Created on: Sep 11, 2023
 *      Author: luke
 */

#include <string.h>
#include <usbh_core.h>

#include "dx/eth2usb/active_servo_class.h"
#include "dx/eth2usb/app.h"
#include "logging.h"
#include "settings.h"
#include "main.h"

// TODO: Figure out why read() can return -1 with errno 0. For now this weird behavior
//  won't cause trouble, since my code will ignore errno 0 (dirty hack!).

extern USBH_HandleTypeDef hUsbHostHS;
extern bool DX_USBH_IsDeviceConnected;

void DX_ETH2USB_App_Init_CreateMemPools(DX_ETH2USB_AppState_t *app) {
	app->commandMemPoolId = osMemoryPoolNew(
	DX_ETH2USB__APP__COMMAND_MEM_POOL_SIZE, sizeof(DX_ETH2USB_Command_t), NULL);
	if (app->commandMemPoolId == NULL)
		Error_Handler();

	app->responseMemPoolId = osMemoryPoolNew(
	DX_ETH2USB__APP__RESPONSE_MEM_POOL_SIZE, sizeof(DX_ETH2USB_Response_t),
	NULL);
	if (app->responseMemPoolId == NULL)
		Error_Handler();
}

void DX_ETH2USB_App_Init_CreateMsgQueues(DX_ETH2USB_AppState_t *app) {
	app->commandMsgQueueId = osMessageQueueNew(
	DX_ETH2USB__APP__COMMAND_MSG_QUEUE_SIZE, sizeof(DX_ETH2USB_Command_t*),
	NULL);
	if (app->commandMsgQueueId == NULL)
		Error_Handler();

	app->responseMsgQueueId = osMessageQueueNew(
	DX_ETH2USB__APP__RESPONSE_MSG_QUEUE_SIZE, sizeof(DX_ETH2USB_Response_t*),
	NULL);
	if (app->responseMsgQueueId == NULL)
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

	ethThreadState->command = NULL;
	ethThreadState->response = NULL;
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

static void DX_ETH2USB_App_EthThread_InitializeWritingOfResponse(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	uint32_t responseCount = 0;
	osStatus_t status = osOK;

	responseCount = osMessageQueueGetCount(app->responseMsgQueueId);
	if (responseCount == 0)
		return;

	status = osMessageQueueGet(app->responseMsgQueueId, &threadState->response,
	NULL, osWaitForever);
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

static void DX_ETH2USB_App_EthThread_WriteResponse_HandleSuccess(
		DX_ETH2USB_AppState_t *app, int32_t ret) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	osStatus status = osOK;

	threadState->nBytesWritten += (uint32_t) ret;

	mlog("Wrote %u out of %u bytes", threadState->nBytesWritten,
			sizeof(DX_ETH2USB_Response_t));

	if (threadState->nBytesWritten < sizeof(DX_ETH2USB_Response_t))
		return;

	status = osMemoryPoolFree(app->responseMemPoolId, threadState->response);
	if (status != osOK) {
		mlog("Failed to free response, status: %d", status);
		Error_Handler();
	}

	threadState->response = NULL;
}

static void DX_ETH2USB_App_EthThread_WriteResponse_EndOfStream(
		DX_ETH2USB_AppState_t *app) {
	mlog("Received EOS while writing response");

	DX_ETH2USB_App_EthThread_CloseClientSocket(app);
}

static void DX_ETH2USB_App_EthThread_WriteResponse_HandleError(
		DX_ETH2USB_AppState_t *app) {
	if (errno == EAGAIN || errno == 0)
		return;
	else if (errno == ECONNRESET) {
		mlog("Connection got closed while writing response");
		DX_ETH2USB_App_EthThread_CloseClientSocket(app);
	} else {
		mlog("Failed to write response, error (%d): %s", errno, strerror(errno));
		Error_Handler();
	}
}

static void DX_ETH2USB_App_EthThread_WriteResponse(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	DX_ETH2USB_App_EthThread_ClientState_t *client = &threadState->client;
	int32_t ret = -1;

	const uint8_t *bytes =
			&((uint8_t*) threadState->response)[threadState->nBytesWritten];
	const uint32_t bytesToWrite = sizeof(DX_ETH2USB_Response_t)
			- threadState->nBytesWritten;

	ret = write(client->fd, bytes, bytesToWrite);

	if (ret > 0) {
		DX_ETH2USB_App_EthThread_WriteResponse_HandleSuccess(app, ret);
	} else if (ret == 0) {
		DX_ETH2USB_App_EthThread_WriteResponse_EndOfStream(app);
	} else if (ret == -1) {
		DX_ETH2USB_App_EthThread_WriteResponse_HandleError(app);
	}
}

static void DX_ETH2USB_App_EthThread_ReadCommand_HandleSuccess_ForwardToUSB(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	osStatus_t status = osOK;

	status = osMessageQueuePut(app->commandMsgQueueId, &threadState->command,
			0U, osWaitForever);
	if (status != osOK) {
		Error_Handler();
	}

	threadState->command = NULL;
}

static void DX_ETH2USB_App_EthThread_ReadCommand_HandleSuccess(
		DX_ETH2USB_AppState_t *app, int32_t ret) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;

	threadState->nBytesRead += (uint32_t) ret;

	mlog("Read %u bytes out of the %u bytes", ret,
			sizeof(DX_ETH2USB_Command_t));

	if (threadState->nBytesRead < sizeof(DX_ETH2USB_Command_t))
		return;

	mlog("Received entire command of size %u", threadState->nBytesRead);

	DX_ETH2USB_App_EthThread_ReadCommand_HandleSuccess_ForwardToUSB(
			app);
}

static void DX_ETH2USB_App_EthThread_ReadCommand_HandleEndOfStream(
		DX_ETH2USB_AppState_t *app) {
	mlog("Received end of stream while reading incoming command");

	DX_ETH2USB_App_EthThread_CloseClientSocket(app);
}

static void DX_ETH2USB_App_EthThread_ReadCommand_HandleError(
		DX_ETH2USB_AppState_t *app) {

	if (errno == EAGAIN || errno == 0)
		return;
	else if (errno == ECONNRESET) {
		mlog("Remote closed stream while reading incoming command");
		DX_ETH2USB_App_EthThread_CloseClientSocket(app);
	} else {
		mlog("Failed to read incoming command, error (%d): %s", errno,
				strerror(errno));
		Error_Handler();
	}
}

static void DX_ETH2USB_App_EthThread_StartReadingCommand(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;

	if (osMemoryPoolGetSpace(app->commandMemPoolId) == 0)
		return;

	threadState->command = osMemoryPoolAlloc(app->commandMemPoolId, 0U);
	if (threadState->command == NULL)
		Error_Handler();

	threadState->nBytesRead = 0;
}

static void DX_ETH2USB_App_EthThread_ReadCommand(
		DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_EthThreadState_t *threadState = &app->ethThreadState;
	DX_ETH2USB_App_EthThread_ClientState_t *client = &threadState->client;
	int32_t ret = -1;

	const uint32_t nBytesToRead = sizeof(DX_ETH2USB_Command_t) - threadState->nBytesRead;
	uint8_t *bytes = &((uint8_t*) threadState->command)[threadState->nBytesRead];

	ret = read(client->fd, bytes, nBytesToRead);

	if (ret > 0) {
		DX_ETH2USB_App_EthThread_ReadCommand_HandleSuccess(app, ret);
	} else if (ret == 0) {
		DX_ETH2USB_App_EthThread_ReadCommand_HandleEndOfStream(app);
	} else if (ret == -1) {
		DX_ETH2USB_App_EthThread_ReadCommand_HandleError(app);
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

		if (threadState->response == NULL)
			DX_ETH2USB_App_EthThread_InitializeWritingOfResponse(app);

		if (threadState->response != NULL)
			DX_ETH2USB_App_EthThread_WriteResponse(app);

		if (client->fd == -1)
			continue;

		if (threadState->command == NULL)
			DX_ETH2USB_App_EthThread_StartReadingCommand(app);
		if (threadState->command != NULL)
			DX_ETH2USB_App_EthThread_ReadCommand(app);

		osDelay(1);
	}
}

static void DX_ETH2USB_App_UsbThread_GetCommand(DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_UsbThreadState_t *threadState = &app->usbThreadState;
	osStatus_t status = osOK;

	status = osMessageQueueGet(app->commandMsgQueueId, &threadState->command,
	NULL, osWaitForever);
	if (status != osOK)
		Error_Handler();
}

static void DX_ETH2USB_App_UsbThread_PutResponse(DX_ETH2USB_AppState_t *app) {
	DX_ETH2USB_App_UsbThreadState_t *threadState = &app->usbThreadState;
	osStatus status = osOK;

	status = osMessageQueuePut(app->responseMsgQueueId, &threadState->response, 0U,
	osWaitForever);
	if (status != osOK)
		Error_Handler();

	threadState->response = NULL;
}

static void DX_ETH2USB_App_UsbThread(void *arg) {
	DX_ETH2USB_AppState_t *app = arg;
	DX_ETH2USB_App_UsbThreadState_t *threadState = &app->usbThreadState;
	DX_ActiveServoClass_StatusTypeDef activeServoClassStatus =
			DX__ACTIVE_SERVO_CLASS__OK;

	while (true) {
		if (!DX_USBH_IsDeviceConnected) {
			osDelay(50);
			continue;
		}

		DX_ETH2USB_App_UsbThread_GetCommand(app);

		uint8_t *in = NULL;

		if (!threadState->command->header.wrOnly) {
			threadState->response = osMemoryPoolAlloc(app->responseMemPoolId, 0U);
			if (threadState->response == NULL)
				Error_Handler();

			in = threadState->response->payload;
		}

		activeServoClassStatus = DX_ActiveServoClass_Cmd(&hUsbHostHS,
				threadState->command->payload, in);
		if (activeServoClassStatus != DX__ACTIVE_SERVO_CLASS__OK) {
			mlog("Failed to command active servo");
			return;
		}

		osMemoryPoolFree(app->commandMemPoolId, threadState->command);

		if (!threadState->command->header.wrOnly) {
			DX_ETH2USB_App_UsbThread_PutResponse(app);
		}

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
