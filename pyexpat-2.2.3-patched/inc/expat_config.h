/* We are not using a "configure" script for Symbian builds,
   so instead we define the configuration here. 

   These settings seem okay for Symbian 7.0s, at least. They were
   taken more or less directly from SyExpat, see
   http://www.toddsoftware.com/ for more information.

   Note that the XML_UNICODE option is presently commented out--
   should look more carefully into Expat Unicode support, and
   how PyExpat maps Expat Unicode strings to Python Unicode strings. XXX
*/

#ifndef __EXPAT_CONFIG_H__
#define __EXPAT_CONFIG_H__

/* 	Copyright 2000, Clark Cooper
   	All rights reserved.

   	This is free software. You are permitted to copy, distribute, or modify
   	it under the terms of the MIT/X license (contained in the COPYING file
   	with this distribution.)
*/

#if !defined(__E32DEF_H__) && defined(NULL)
#undef NULL
#endif
#include <e32def.h>

// Symbian is little endian
#define BYTEORDER 1234

#undef HAVE_CHECK_H

/* We do not have the <dlfcn.h> header file. */
#undef HAVE_DLFCN_H
#define HAVE_FCNTL_H 1

// getpagesize function is in unistd.h
#define HAVE_GETPAGESIZE 1

/* The the <inttypes.h> header file is not present. */
#undef HAVE_INTTYPES_H

/* Symbian has memmove and also bcopy if not using strict ansi*/
#define HAVE_MEMMOVE 1

#undef HAVE_MEMORY_H

/* Symbian does not support the `mmap' system call. */
#undef HAVE_MMAP

/* Symbian  does not have the <stdint.h> header file. */
#undef HAVE_STDINT_H

/* But it does have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* We do not have <strings.h> header file. */
#undef HAVE_STRINGS_H

/* But we do have the <string.h> header file. */
#define HAVE_STRING_H 1

/* We have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* We  have the <sys/types.h> header file. */
#undef HAVE_SYS_TYPES_H

/* We have the <unistd.h> header file. */
#undef HAVE_UNISTD_H

/* we  have the ANSI C header files. */
#define STDC_HEADERS 1

/* Symbian is little endian */
#undef WORDS_BIGENDIAN

/* We do not use XML context buffers to save memory*/
#undef XML_CONTEXT_BYTES	

// We want unicode output for string etc--
// no we don't, as this option seems to break things--
// at least basic_test.py produces garbageish output,
// and PDIS no longer works with this option
//#define XML_UNICODE 1

/* We do not currently want DTD support. */
#undef XML_DTD

/* We want Namspace Support */
#define XML_NS 1

/* Make sure const is defined at the compiler level and not defined */
#undef const

/* defined in sys\types.h */
#undef off_t

/* defined in sys\types.h */
#undef size_t

#ifndef NULL
#define NULL 0L
#endif

#endif // __EXPAT_CONFIG_H__
