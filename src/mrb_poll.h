#ifndef MRB_POLL_H
#define MRB_POLL_H

#include <stdlib.h>
#include <mruby.h>
#include <mruby/data.h>
#include <mruby/array.h>
#include <mruby/variable.h>
#include <poll.h>
#include <errno.h>
#include <mruby/error.h>
#include <mruby/class.h>
#include <unistd.h>

static void
mrb_poll_free(mrb_state *mrb, void *p)
{
  free(p);
}

static const struct mrb_data_type mrb_poll_type = {
  "$i_mrb_poll", mrb_poll_free
};

static const struct mrb_data_type mrb_pollfd_type = {
  "$i_mrb_pollfd", NULL
};

#endif
