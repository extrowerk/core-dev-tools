#ifndef __NTO_INTTYPES__
#define __NTO_INTTYPES__

#ifdef __QNX__
#include <inttypes.h>
#else
#ifdef sun
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
#endif
#ifndef __CYGWIN32__
typedef unsigned char		uint8_t;
typedef unsigned short		uint16_t;
typedef unsigned int		uint32_t;
typedef unsigned long long	uint64_t;
#endif
#endif

#endif
