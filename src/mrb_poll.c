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
  if (unlikely(DATA_PTR(self))) {
    mrb_free(mrb, DATA_PTR(self));
  }

  mrb_data_init(self, NULL, NULL);

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "fds"), mrb_ary_new_capa(mrb, 1));

  return self;
}

static mrb_value
mrb_poll_add(mrb_state *mrb, mrb_value self)
{
  mrb_value socket, fds, pollfd_obj;
  mrb_int events = POLLIN, fd;
  struct pollfd *pollfds = NULL, *pollfd_ = NULL;

  mrb_get_args(mrb, "o|i", &socket, &events);

  fd = mrb_fixnum(mrb_Integer(mrb, socket));

  mrb_assert(fd >= INT_MIN&&fd <= INT_MAX);
  mrb_assert(events >= INT_MIN&&events <= INT_MAX);

  fds = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "fds"));
  mrb_assert(mrb_type(fds) == MRB_TT_ARRAY);

  if (likely(RARRAY_LEN(fds) + 1 > 0 &&
      RARRAY_LEN(fds) + 1 <= SIZE_MAX / sizeof(struct pollfd))) {

    pollfds = (struct pollfd *) mrb_realloc(mrb, DATA_PTR(self), (RARRAY_LEN(fds) + 1) * sizeof(struct pollfd));
  } else {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "cannot add more fds");
  }
  if (DATA_PTR(self) && DATA_PTR(self) != pollfds) {
    mrb_int i;
    for (i = 0; i < RARRAY_LEN(fds); ++i) {
      mrb_data_init(mrb_ary_ref(mrb, fds, i), &pollfds[i], &mrb_pollfd_type);
    }
  }
  mrb_data_init(self, pollfds, &mrb_poll_type);
  pollfd_ = &pollfds[RARRAY_LEN(fds)];
  pollfd_->fd = fd;
  pollfd_->events = events;
  pollfd_->revents = 0;

  pollfd_obj = mrb_obj_new(mrb, mrb_class_get_under(mrb, mrb_class(mrb, self), "_Fd"), 1, &socket);
  mrb_data_init(pollfd_obj, pollfd_, &mrb_pollfd_type);
  mrb_ary_push(mrb, fds, pollfd_obj);

  return pollfd_obj;
}

static mrb_value
mrb_poll_remove(mrb_state *mrb, mrb_value self)
{
  mrb_value fds, pollfd_obj;
  struct pollfd *pollfds = NULL;

  if (unlikely(!DATA_PTR(self))) {
    return mrb_false_value();
  }

  fds = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "fds"));
  mrb_assert(mrb_type(fds) == MRB_TT_ARRAY);
  pollfds = (struct pollfd *) DATA_PTR(self);

  mrb_get_args(mrb, "o", &pollfd_obj);
  mrb_data_get_ptr(mrb, pollfd_obj, &mrb_pollfd_type);

  if (RARRAY_LEN(fds) == 1 && mrb_obj_id(pollfd_obj) == mrb_obj_id(mrb_ary_ref(mrb, fds, 0))) {
    mrb_ary_clear(mrb, fds);
    mrb_free(mrb, DATA_PTR(self));
    mrb_data_init(self, NULL, NULL);
    mrb_data_init(pollfd_obj, NULL, NULL);
    mrb_iv_remove(mrb, pollfd_obj, mrb_intern_lit(mrb, "socket"));

    return self;
  } else {
    for (mrb_int i = 0; i < RARRAY_LEN(fds); i++) {
      if (mrb_obj_id(pollfd_obj) == mrb_obj_id(mrb_ary_ref(mrb, fds, i))) {
        struct pollfd *ptr = pollfds + i;
        mrb_int len = RARRAY_LEN(fds) - i;
        while (--len) {
          *ptr = *(ptr+1);
          ++ptr;
        }
        mrb_funcall(mrb, fds, "delete_at", 1, mrb_fixnum_value(i));
        pollfds = (struct pollfd *) mrb_realloc(mrb, DATA_PTR(self), RARRAY_LEN(fds) * sizeof(struct pollfd));
        if (DATA_PTR(self) != pollfds) {
          mrb_int j;
          for (j = 0; j < RARRAY_LEN(fds); j++) {
            mrb_data_init(mrb_ary_ref(mrb, fds, j), &pollfds[j], &mrb_pollfd_type);
          }
          mrb_data_init(self, pollfds, &mrb_poll_type);
        }
        mrb_data_init(pollfd_obj, NULL, NULL);
        mrb_iv_remove(mrb, pollfd_obj, mrb_intern_lit(mrb, "socket"));

        return self;
      }
    }
  }

  return mrb_nil_value();
}

static mrb_value
mrb_poll_clear(mrb_state *mrb, mrb_value self)
{
  mrb_value fds;

  if (!DATA_PTR(self)) {
    return self;
  }

  fds = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "fds"));
  mrb_assert(mrb_type(fds) == MRB_TT_ARRAY);

  for (mrb_int i = 0; i < RARRAY_LEN(fds); i++) {
    mrb_value pollfd_obj = mrb_ary_ref(mrb, fds, i);
    mrb_data_init(pollfd_obj, NULL, NULL);
    mrb_iv_remove(mrb, pollfd_obj, mrb_intern_lit(mrb, "socket"));
  }

  mrb_ary_clear(mrb, fds);
  mrb_free(mrb, DATA_PTR(self));
  mrb_data_init(self, NULL, NULL);

  return self;
}

static mrb_value
mrb_poll_wait(mrb_state *mrb, mrb_value self)
{
  mrb_value fds;
  mrb_int timeout = -1;
  mrb_value block = mrb_nil_value();
  struct pollfd *pollfds = NULL;
  int ret;

  mrb_get_args(mrb, "|i&", &timeout, &block);

  mrb_assert(timeout >= INT_MIN&&timeout <= INT_MAX);

  fds = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "fds"));
  mrb_assert(mrb_type(fds) == MRB_TT_ARRAY);

  pollfds = (struct pollfd *) DATA_PTR(self);

  errno = 0;
  ret = poll(pollfds, RARRAY_LEN(fds), timeout);

  if (ret > 0) {
    if (mrb_type(block) == MRB_TT_PROC) {
      for (mrb_int i = 0; i < RARRAY_LEN(fds); i++) {
        if (pollfds[i].revents) {
          mrb_yield(mrb, block, mrb_ary_ref(mrb, fds, i));
        }
      }
    } else {
      mrb_value ready_fds = mrb_ary_new_capa(mrb, ret);
      for (mrb_int i = 0; i < RARRAY_LEN(fds); i++) {
        if (pollfds[i].revents) {
          mrb_ary_push(mrb, ready_fds, mrb_ary_ref(mrb, fds, i));
        }
      }
      return ready_fds;
    }
  }
  else if (ret == 0) {
    return mrb_nil_value();
  }
  else if (unlikely(ret == -1)) {
    if (errno == EINTR) {
      return mrb_false_value();
    } else {
      mrb_sys_fail(mrb, "poll");
    }
  }
  else {
    mrb_raise(mrb, E_RUNTIME_ERROR, "poll returned erroneous value");
  }

  return self;
}

static mrb_value
mrb_pollfd_init(mrb_state *mrb, mrb_value self)
{
  mrb_value socket;

  if (unlikely(DATA_PTR(self))) {
    mrb_free(mrb, DATA_PTR(self));
  }

  mrb_data_init(self, NULL, NULL);

  mrb_get_args(mrb, "o", &socket);

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "socket"), socket);

  return self;
}

static mrb_value
mrb_pollfd_socket(mrb_state *mrb, mrb_value self)
{
  mrb_assert(DATA_PTR(self));

  return mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "socket"));
}

static mrb_value
mrb_pollfd_set_socket(mrb_state *mrb, mrb_value self)
{
  mrb_value socket;
  mrb_int fd;

  mrb_assert(DATA_PTR(self));

  mrb_get_args(mrb, "o", &socket);

  fd = mrb_fixnum(mrb_Integer(mrb, socket));

  mrb_assert(fd >= INT_MIN&&fd <= INT_MAX);

  ((struct pollfd *) DATA_PTR(self))->fd = fd;

  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "socket"), socket);

  return self;
}

static mrb_value
mrb_pollfd_events(mrb_state *mrb, mrb_value self)
{
  mrb_assert(DATA_PTR(self));

  return mrb_fixnum_value(((struct pollfd *) DATA_PTR(self))->events);
}

static mrb_value
mrb_pollfd_set_events(mrb_state *mrb, mrb_value self)
{
  mrb_int events;

  mrb_assert(DATA_PTR(self));

  mrb_get_args(mrb, "i", &events);

  mrb_assert(events >= INT_MIN&&events <= INT_MAX);

  ((struct pollfd *) DATA_PTR(self))->events = events;

  return self;
}

static mrb_value
mrb_pollfd_revents(mrb_state *mrb, mrb_value self)
{
  mrb_assert(DATA_PTR(self));

  return mrb_fixnum_value(((struct pollfd *) DATA_PTR(self))->revents);
}

static mrb_value
mrb_pollfd_set_revents(mrb_state *mrb, mrb_value self)
{
  mrb_int revents;

  mrb_assert(DATA_PTR(self));

  mrb_get_args(mrb, "i", &revents);

  mrb_assert(revents >= INT_MIN&&revents <= INT_MAX);

  ((struct pollfd *) DATA_PTR(self))->revents = revents;

  return self;
}

static mrb_value
mrb_pollfd_readable(mrb_state *mrb, mrb_value self)
{
  mrb_assert(DATA_PTR(self));

  return mrb_bool_value(((struct pollfd *) DATA_PTR(self))->revents & POLLIN);
}

static mrb_value
mrb_pollfd_writable(mrb_state *mrb, mrb_value self)
{
  mrb_assert(DATA_PTR(self));

  return mrb_bool_value(((struct pollfd *) DATA_PTR(self))->revents & POLLOUT);
}

static mrb_value
mrb_pollfd_error(mrb_state *mrb, mrb_value self)
{
  mrb_assert(DATA_PTR(self));

  return mrb_bool_value(((struct pollfd *) DATA_PTR(self))->revents & POLLERR);
}

static mrb_value
mrb_pollfd_disconnected(mrb_state *mrb, mrb_value self)
{
  mrb_assert(DATA_PTR(self));

  return mrb_bool_value(((struct pollfd *) DATA_PTR(self))->revents & POLLHUP);
}


void
mrb_mruby_poll_gem_init(mrb_state *mrb)
{
  struct RClass *poll_class, *pollfd_class;
  poll_class = mrb_define_class(mrb, "Poll", mrb->object_class);
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

  mrb_define_method(mrb, poll_class, "initialize",  mrb_poll_init, MRB_ARGS_NONE());
  mrb_define_method(mrb, poll_class, "add",         mrb_poll_add, MRB_ARGS_ARG(1, 1));
  mrb_define_method(mrb, poll_class, "remove",      mrb_poll_remove, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, poll_class, "clear",       mrb_poll_clear, MRB_ARGS_NONE());
  mrb_define_method(mrb, poll_class, "wait",        mrb_poll_wait, MRB_ARGS_OPT(1)|MRB_ARGS_BLOCK());

  pollfd_class = mrb_define_class_under(mrb, poll_class, "_Fd", mrb->object_class);
  MRB_SET_INSTANCE_TT(pollfd_class, MRB_TT_DATA);
  mrb_define_method(mrb, pollfd_class, "initialize",  mrb_pollfd_init, MRB_ARGS_REQ(3));
  mrb_define_method(mrb, pollfd_class, "socket", mrb_pollfd_socket, MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_class, "socket=", mrb_pollfd_set_socket, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, pollfd_class, "events", mrb_pollfd_events, MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_class, "events=", mrb_pollfd_set_events, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, pollfd_class, "revents", mrb_pollfd_revents, MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_class, "revents=", mrb_pollfd_set_revents, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, pollfd_class, "readable?", mrb_pollfd_readable, MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_class, "writable?", mrb_pollfd_writable, MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_class, "error?", mrb_pollfd_error, MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_class, "disconnected?", mrb_pollfd_disconnected, MRB_ARGS_NONE());

  mrb_define_const(mrb, mrb->kernel_module, "STDIN_FILENO", mrb_fixnum_value(STDIN_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDOUT_FILENO", mrb_fixnum_value(STDOUT_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDERR_FILENO", mrb_fixnum_value(STDERR_FILENO));
}

void mrb_mruby_poll_gem_final(mrb_state *mrb) {}
