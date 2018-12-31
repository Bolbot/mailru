#ifndef __DEFINES_H__
#define __DEFINES_H__

#define PATHSIZE 1024
#define DATELENGTH 256
#define MIMELENGTH 128
#define SELSOCKS 1024
#define POLLSIZE 8192
#define EPOLLSIZE 4096
#define MAXPROCESS 128

#if !defined(VERBOSE)
#define VERBOSE 1
#endif

#if !defined(VERBOSE_UTILS)
#define VERBOSE_UTILS VERBOSE
#endif

#endif
