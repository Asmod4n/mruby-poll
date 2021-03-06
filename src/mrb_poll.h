#ifndef MRB_POLL_H
#define MRB_POLL_H

#include <stdlib.h>
#include <mruby.h>
#ifdef MRB_INT16
#error "MRB_INT16 is too small for mruby-poll"
#endif
#include <mruby/data.h>
#include <mruby/array.h>
#include <mruby/variable.h>
#include <poll.h>
#include <errno.h>
#include <mruby/error.h>
#include <mruby/class.h>
#include <unistd.h>

static const struct mrb_data_type mrb_poll_type = {
  "$i_mrb_poll", mrb_free
};

static const struct mrb_data_type mrb_pollfd_type = {
  "$i_mrb_pollfd", NULL
};

#endif
