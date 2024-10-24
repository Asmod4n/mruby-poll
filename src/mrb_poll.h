#ifndef MRB_POLL_H
#define MRB_POLL_H

#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
  #include <winerror.h>
#else
  #include <poll.h>
  #include <unistd.h>
#endif

#include <stdlib.h>
#include <mruby.h>
#ifdef _WIN32
  #if MRB_INT_BIT < 64
  #error "needs 64 bit mruby on windows"
  #endif
#else
  #if MRB_INT_BIT < 32
  #error "needs at least 32 bit mruby"
  #endif
#endif
#include <mruby/hash.h>
#include <mruby/array.h>
#include <mruby/variable.h>
#include <errno.h>
#include <mruby/error.h>
#include <mruby/class.h>

#include <string.h>
#include <mruby/throw.h>

#endif
