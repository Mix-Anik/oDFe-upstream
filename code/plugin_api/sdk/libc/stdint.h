/* The types of this compiler are all 32 bit or smaller: there are no 64 bit integers (long long is only 32 bits wide!) and
double is a float. */

#ifndef ODF_STDINT_H
#define ODF_STDINT_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;

#define INT8_MIN ( -128 )
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN ( -32768 )
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT32_MIN ( -2147483647 - 1 )
#define INT32_MAX 2147483647
#define UINT32_MAX 0xFFFFFFFFu

#endif
