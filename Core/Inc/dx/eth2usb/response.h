/*
 * response.h
 *
 *  Created on: Sep 14, 2023
 *      Author: luke
 */

#ifndef INC_DX_ETH2USB_RESPONSE_H_
#define INC_DX_ETH2USB_RESPONSE_H_

#include <stdint.h>

#include "settings.h"

#define DX__ETH2USB__RESPONSE__PAYLOAD_BUFFER_SIZE DX_ETH2USB__MAX_PACKET_SIZE

typedef struct __attribute__ (( packed )) {
	uint8_t payload[DX__ETH2USB__RESPONSE__PAYLOAD_BUFFER_SIZE];
} DX_ETH2USB_Response_t;

#endif /* INC_DX_ETH2USB_RESPONSE_H_ */
