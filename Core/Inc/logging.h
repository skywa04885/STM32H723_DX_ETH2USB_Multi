/*
 * logging.h
 *
 *  Created on: Sep 8, 2023
 *      Author: luke
 */

#ifndef INC_LOGGING_H_
#define INC_LOGGING_H_

#define mlog(...) \
	_mlog(__func__, __LINE__, __VA_ARGS__)

void _mlog(const char *function, const int line, const char *fmt, ...);

#endif /* INC_LOGGING_H_ */
