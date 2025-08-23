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
#include <mruby/value.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include <mruby/variable.h>
#include <errno.h>
#include <mruby/error.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/gc.h>
#include <string.h>

#include <new>
#include <vector>
#include <algorithm>
#include <functional>
#include <mruby/cpp_helpers.hpp>
#include <mruby/num_helpers.hpp>

#endif
