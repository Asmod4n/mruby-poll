#include "mrb_poll.h"

#if (__GNUC__ >= 3) || (__INTEL_COMPILER >= 800) || defined(__clang__)
# define likely(x) __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x) (x)
# define unlikely(x) (x)
#endif

static mrb_value
mrb_poll_init(mrb_state *mrb, mrb_value self)
{
  if (DATA_PTR(self)) {
    mrb_free(mrb, DATA_PTR(self));
  }

  mrb_data_init(self, NULL, NULL);

  mrb_sym fds_sym = mrb_intern_lit(mrb, "@fds");
  mrb_value fds = mrb_ary_new_capa(mrb, 1);

  mrb_iv_set(mrb, self, fds_sym, fds);

  return self;
}

static mrb_value
mrb_poll_update(mrb_state *mrb, mrb_value self)
{
  if (likely(DATA_PTR(self))) {
    free(DATA_PTR(self));
    mrb_data_init(self, NULL, NULL);
  }

  mrb_value fds = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@fds"));

  struct pollfd *pollfds = (struct pollfd *) calloc(RARRAY_LEN(fds), sizeof(struct pollfd));
  if (unlikely(!pollfds)) {
    mrb_sys_fail(mrb, "calloc");
  }
  mrb_data_init(self, pollfds, &mrb_poll_type);

  for (mrb_int i = 0; i < RARRAY_LEN(fds); i++) {
    mrb_value fd_val = mrb_funcall(mrb, mrb_ary_ref(mrb, fds, i), "fd", 0);
    mrb_int fd = mrb_int(mrb, fd_val);
    if (unlikely(fd < INT_MIN||fd > INT_MAX)) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "fd doesn't fit into INT");
    }
    pollfds[i].fd = fd;
    mrb_value events_val = mrb_funcall(mrb, mrb_ary_ref(mrb, fds, i), "events", 0);
    mrb_int events = mrb_int(mrb, events_val);
    if (unlikely(events < INT_MIN||events > INT_MAX)) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "events doesn't fit into INT");
    }
    pollfds[i].events = events;
  }

  return self;
}

static mrb_value
mrb_poll_wait(mrb_state *mrb, mrb_value self)
{
  if (unlikely(!DATA_PTR(self))) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "No fds registered for polling");
  }

  mrb_int timeout = -1;

  mrb_get_args(mrb, "|i", &timeout);

  if (unlikely(timeout < INT_MIN||timeout > INT_MAX)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "timeout doesn't fit into INT");
  }

  mrb_value fds = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@fds"));

  int poll_retry = 0;
mrb_poll_retry:
  int ret = poll((struct pollfd *) DATA_PTR(self), RARRAY_LEN(fds), timeout);

  switch(ret) {
    case -1: {
      if (errno == EAGAIN && ++poll_retry < 42) {
        goto mrb_poll_retry;
      }
      if (errno == EINTR) {
        return mrb_false_value();
      } else {
        mrb_sys_fail(mrb, "poll");
      }
    } break;
    case 0: {
      return mrb_nil_value();
    }
    default: {
      struct pollfd *pollfds = (struct pollfd *) DATA_PTR(self);
      for (mrb_int i = 0; i < RARRAY_LEN(fds); i++) {
        if (pollfds[i].revents) {
          mrb_funcall(mrb, mrb_ary_ref(mrb, fds, i), "revents=", 1, mrb_fixnum_value(pollfds[i].revents));
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
  mrb_define_const(mrb, poll_class, "Err", mrb_fixnum_value(POLLERR));
  mrb_define_const(mrb, poll_class, "Hup", mrb_fixnum_value(POLLHUP));
  mrb_define_const(mrb, poll_class, "In", mrb_fixnum_value(POLLIN));
  mrb_define_const(mrb, poll_class, "Nval", mrb_fixnum_value(POLLNVAL));
  mrb_define_const(mrb, poll_class, "Out", mrb_fixnum_value(POLLOUT));
  mrb_define_const(mrb, poll_class, "Pri", mrb_fixnum_value(POLLPRI));
  mrb_define_const(mrb, poll_class, "RdBand", mrb_fixnum_value(POLLRDBAND));
  mrb_define_const(mrb, poll_class, "RdNorm", mrb_fixnum_value(POLLRDNORM));
  mrb_define_const(mrb, poll_class, "WrBand", mrb_fixnum_value(POLLWRBAND));
  mrb_define_const(mrb, poll_class, "WrNorm", mrb_fixnum_value(POLLWRNORM));

  mrb_define_method(mrb, poll_class, "initialize", mrb_poll_init, MRB_ARGS_NONE());
  mrb_define_method(mrb, poll_class, "__update", mrb_poll_update, MRB_ARGS_NONE());
  mrb_define_method(mrb, poll_class, "wait", mrb_poll_wait, MRB_ARGS_OPT(1));

  mrb_define_const(mrb, mrb->kernel_module, "STDIN_FILENO", mrb_fixnum_value(STDIN_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDOUT_FILENO", mrb_fixnum_value(STDOUT_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDERR_FILENO", mrb_fixnum_value(STDERR_FILENO));
}

void mrb_mruby_poll_gem_final(mrb_state *mrb) {}
