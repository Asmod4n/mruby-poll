#include <mruby.h>
#include <mruby/data.h>
#include <mruby/array.h>
#include <mruby/variable.h>
#include <poll.h>
#include <mruby/error.h>
#include <mruby/class.h>
#include <unistd.h>

static mrb_value
mrb_poll_init(mrb_state *mrb, mrb_value self)
{
  if (DATA_PTR(self)) {
    mrb_free(mrb, DATA_PTR(self));
  }

  mrb_data_init(self, NULL, NULL);

  mrb_sym pollset_sym = mrb_intern_lit(mrb, "@pollset");
  mrb_value pollset = mrb_ary_new_capa(mrb, 1);

  mrb_iv_set(mrb, self, pollset_sym, pollset);

  return self;
}

static const struct mrb_data_type mrb_poll_type = {
  "$i_mrb_poll", mrb_free
};

static mrb_value
mrb_poll_update(mrb_state *mrb, mrb_value self)
{
  if (DATA_PTR(self)) {
    mrb_free(mrb, DATA_PTR(self));
  }

  mrb_value pollset = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@pollset"));

  struct pollfd *pollfds = (struct pollfd *) mrb_calloc(mrb, RARRAY_LEN(pollset), sizeof(struct pollfd));
  if (!pollfds) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "cannot allocate pollfds");
  }
  mrb_data_init(self, pollfds, &mrb_poll_type);

  for (mrb_int i = 0; i < RARRAY_LEN(pollset); i++) {
    mrb_value fd_val = mrb_funcall(mrb, mrb_ary_ref(mrb, pollset, i), "fd", 0);
    mrb_int fd = mrb_int(mrb, fd_val);
    if (fd < INT_MIN||fd > INT_MAX) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "fd doesn't fit into INT");
    }
    pollfds[i].fd = fd;
    mrb_value events_val = mrb_funcall(mrb, mrb_ary_ref(mrb, pollset, i), "events", 0);
    mrb_int events = mrb_int(mrb, events_val);
    if (events < INT_MIN||events > INT_MAX) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "events doesn't fit into INT");
    }
    pollfds[i].events = events;
  }

  return self;
}

static mrb_value
mrb_poll_wait(mrb_state *mrb, mrb_value self)
{
  if (!DATA_PTR(self)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "No pollfds registered for polling");
  }

  mrb_int timeout = -1;

  mrb_get_args(mrb, "|i", &timeout);

  if (timeout < INT_MIN||timeout > INT_MAX) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "timeout doesn't fit into INT");
  }

  mrb_value pollset = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@pollset"));

  int ret = poll((struct pollfd *) DATA_PTR(self), RARRAY_LEN(pollset), timeout);

  switch(ret) {
    case -1: {
      mrb_sys_fail(mrb, "poll");
    } break;
    case 0: {
      return mrb_nil_value();
    }
    default: {
      struct pollfd *pollfds = (struct pollfd *) DATA_PTR(self);
      for (mrb_int i = 0; i < RARRAY_LEN(pollset); i++) {
        if (pollfds[i].revents) {
          mrb_funcall(mrb, mrb_ary_ref(mrb, pollset, i), "revents=", 1, mrb_fixnum_value(pollfds[i].revents));
        }
      }
    }
  }

  return mrb_fixnum_value(ret);
}

void
mrb_mruby_poll_gem_init(mrb_state *mrb)
{
  struct RClass *poll_class = mrb_define_class(mrb, "Poll", mrb->object_class);
  MRB_SET_INSTANCE_TT(poll_class, MRB_TT_DATA);
#ifdef POLLERR
  mrb_define_const(mrb, poll_class, "Err", mrb_fixnum_value(POLLERR));
#endif
#ifdef POLLHUP
  mrb_define_const(mrb, poll_class, "Hup", mrb_fixnum_value(POLLHUP));
#endif
#ifdef POLLIN
  mrb_define_const(mrb, poll_class, "In", mrb_fixnum_value(POLLIN));
#endif
#ifdef POLLNVAL
  mrb_define_const(mrb, poll_class, "Nval", mrb_fixnum_value(POLLNVAL));
#endif
#ifdef POLLOUT
  mrb_define_const(mrb, poll_class, "Out", mrb_fixnum_value(POLLOUT));
#endif
#ifdef POLLPRI
  mrb_define_const(mrb, poll_class, "Pri", mrb_fixnum_value(POLLPRI));
#endif
#ifdef POLLRDBAND
  mrb_define_const(mrb, poll_class, "RdBand", mrb_fixnum_value(POLLRDBAND));
#endif
#ifdef POLLRDNORM
  mrb_define_const(mrb, poll_class, "RdNorm", mrb_fixnum_value(POLLRDNORM));
#endif
#ifdef POLLWRBAND
  mrb_define_const(mrb, poll_class, "WrBand", mrb_fixnum_value(POLLWRBAND));
#endif
#ifdef POLLWRNORM
  mrb_define_const(mrb, poll_class, "WrNorm", mrb_fixnum_value(POLLWRNORM));
#endif
  mrb_define_method(mrb, poll_class, "initialize", mrb_poll_init, MRB_ARGS_NONE());
  mrb_define_method(mrb, poll_class, "update", mrb_poll_update, MRB_ARGS_NONE());
  mrb_define_method(mrb, poll_class, "wait", mrb_poll_wait, MRB_ARGS_OPT(1));

  mrb_define_const(mrb, mrb->kernel_module, "STDIN_FILENO", mrb_fixnum_value(STDIN_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDOUT_FILENO", mrb_fixnum_value(STDOUT_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDERR_FILENO", mrb_fixnum_value(STDERR_FILENO));
}

void mrb_mruby_poll_gem_final(mrb_state *mrb) {}
