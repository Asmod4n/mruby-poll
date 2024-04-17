#ifndef MRB_POLL_H
#define MRB_POLL_H

#include <stdlib.h>
#include <mruby.h>
#ifdef MRB_INT16
#error "MRB_INT16 is too small for mruby-poll"
#endif
#include <mruby/hash.h>
#include <mruby/array.h>
#include <mruby/variable.h>
#include <poll.h>
#include <errno.h>
#include <mruby/error.h>
#include <mruby/class.h>
#include <unistd.h>
#include <string.h>
#include <mruby/throw.h>

#endif
