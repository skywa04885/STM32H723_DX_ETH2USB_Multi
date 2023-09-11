/*
 * transaction.h
 *
 *  Created on: Sep 11, 2023
 *      Author: luke
 */

#ifndef INC_ETH_FRAME_H_
#define INC_ETH_FRAME_H_

#include <stdint.h>

#ifndef DX_ETH2USB_ETHFRAME_MAX_PAYLOAD_SIZE
#define DX_ETH2USB_ETHFRAME_MAX_PAYLOAD_SIZE 512
#endif

// We're marking it packed so we can immediately
//  write this to a socket.
typedef struct __attribute__ (( packed )) {
	uint32_t payloadSize;
	uint8_t payload[DX_ETH2USB_ETHFRAME_MAX_PAYLOAD_SIZE];
} DX_ETH2USB_ETHFrame_t;

#endif /* INC_ETH_FRAME_H_ */
