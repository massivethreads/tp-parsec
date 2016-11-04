#ifndef _ATOMIC_H_
#define _ATOMIC_H_

#include <stdint.h>

/* Define several macros which we need */
#ifndef __STRING
#define __STRING(x)     #x              /* stringify without expanding x */
#endif

#ifndef __XSTRING
#define __XSTRING(x)    __STRING(x)     /* expand x, then stringify */
#endif

#define u_char uint8_t 
#define u_short uint16_t 
#define u_int uint32_t 
#define u_long uint64_t 


/* Note: The header files were taken from the source of the BSD kernel. More architectures than listed below are supported by BSD. To add another atomic.h for an architecture ${ARCH}, simply copy the atomic.h file which is located in the sys/${ARCH}/include directory of the kernel source tree. You'll also need any files from that directory on which atomic.h depends, and you'll probably have to slightly adapt the files. */

/* Include the correct atomic.h header file for this machine */

#if defined(__i386__) || defined(__i386) || defined(i386) || defined(__I386__) || defined (_M_IX86)
#  include "i386/atomic.h"
#elif defined(__amd64__) || defined(__amd64) || defined(amd64) || defined(__AMD64__) || defined (_M_X64) || defined (__x86_64)
#  include "amd64/atomic.h"
#elif defined(__powerpc__) || defined(__powerpc) || defined(powerpc) || defined(__POWERPC__)
#  include "powerpc/atomic.h"
#elif defined(__sparc__) || defined(__sparc) || defined(sparc) || defined(__SPARC__)
#  include "sparc/atomic.h"
#elif defined(__ia64__) || defined(__ia64) || defined(ia64) || defined(__IA64__) || defined (_M_IA64)
#  include "ia64/atomic.h"
#elif defined(__alpha__) || defined(__alpha) || defined(alpha) || defined(__ALPHA__)
#  include "alpha/atomic.h"
#else
#  error Architecture not supported by atomic.h
#endif

#endif /* _ATOMIC_H_ */
