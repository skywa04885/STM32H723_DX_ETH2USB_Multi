/*
 * transaction.h
 *
 *  Created on: Sep 11, 2023
 *      Author: luke
 */

#ifndef INC_COMMAND_H_
#define INC_COMMAND_H_

#include <stdint.h>

#include "settings.h"

#define DX__ETH2USB__COMMAND__PAYLOAD_BUFFER_SIZE DX_ETH2USB__MAX_PACKET_SIZE

typedef struct __attribute__ (( packed )) {
	unsigned wrOnly : 1;		/* Indicates that this is a write only command (we don't expect a response). */
	unsigned reserved : 7;		/* Flags are reserved for future usage. */
} DX_ETH2USB_CommandHeader_t;

typedef struct __attribute__ (( packed )) {
	DX_ETH2USB_CommandHeader_t header;
	uint8_t payload[DX__ETH2USB__COMMAND__PAYLOAD_BUFFER_SIZE];
} DX_ETH2USB_Command_t;

#endif /* INC_COMMAND_H_ */
