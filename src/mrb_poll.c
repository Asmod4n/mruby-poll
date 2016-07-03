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

  mrb_sym fds_sym = mrb_intern_lit(mrb, "@fds");
  mrb_value fds = mrb_ary_new_capa(mrb, 1);

  mrb_iv_set(mrb, self, fds_sym, fds);

  return self;
}

static mrb_value
mrb_poll_add(mrb_state *mrb, mrb_value self)
{
  mrb_value socket;
  mrb_int events;

  mrb_get_args(mrb, "oi", &socket, &events);

  if (unlikely(events < INT_MIN||events > INT_MAX)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "events doesn't fit into INT");
  }

  mrb_value fds = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@fds"));
  mrb_assert(mrb_type(fds) == MRB_TT_ARRAY);

  struct pollfd *pollfds = NULL;

  if (likely(RARRAY_LEN(fds) + 1 > 0 &&
      RARRAY_LEN(fds) + 1 <= SIZE_MAX / sizeof(struct pollfd))) {

    pollfds = (struct pollfd *) realloc(DATA_PTR(self), (RARRAY_LEN(fds) + 1) * sizeof(struct pollfd));
  }
  if (unlikely(!pollfds)) {
    mrb_exc_raise(mrb, mrb_obj_value(mrb->nomem_err));
  }
  if (DATA_PTR(self) && DATA_PTR(self) != pollfds) {
    for (mrb_int i = 0; i < RARRAY_LEN(fds); ++i) {
      mrb_data_init(mrb_ary_ref(mrb, fds, i), &pollfds[i], &mrb_pollfd_type);
    }
  }
  mrb_data_init(self, pollfds, &mrb_poll_type);
  struct pollfd *pollfd = &pollfds[RARRAY_LEN(fds)];
  pollfd->fd = mrb_int(mrb, socket);
  pollfd->events = events;
  pollfd->revents = 0;

  mrb_value pollfd_obj = mrb_obj_new(mrb, mrb_class_get_under(mrb, mrb_class(mrb, self), "_Fd"), 1, &socket);
  mrb_data_init(pollfd_obj, pollfd, &mrb_pollfd_type);
  mrb_ary_push(mrb, fds, pollfd_obj);

  return pollfd_obj;
}

static mrb_value
mrb_poll_remove(mrb_state *mrb, mrb_value self)
{
  if (unlikely(!DATA_PTR(self))) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "No fds registered for polling");
  }

  mrb_value fds = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@fds"));
  mrb_assert(mrb_type(fds) == MRB_TT_ARRAY);

  mrb_value pollfd_obj;

  mrb_get_args(mrb, "o", &pollfd_obj);

  struct pollfd *pollfd = (struct pollfd *) mrb_data_get_ptr(mrb, pollfd_obj, &mrb_pollfd_type);

  if (RARRAY_LEN(fds) == 1) {
    mrb_funcall(mrb, fds, "delete_at", 1, mrb_fixnum_value(0));
    free(DATA_PTR(self));
    mrb_data_init(self, NULL, NULL);
    mrb_data_init(pollfd_obj, NULL, NULL);
    mrb_iv_remove(mrb, pollfd_obj, mrb_intern_lit(mrb, "socket"));
  }
  else {
    struct pollfd *pollfds = (struct pollfd *) DATA_PTR(self);

    for (mrb_int i = 0; i < RARRAY_LEN(fds); i++) {
      if (pollfd == &pollfds[i]) {
        struct pollfd *ptr = pollfds + i;
        mrb_int len = RARRAY_LEN(fds) - i;
        while (--len) {
          *ptr = *(ptr+1);
          ++ptr;
        }
        mrb_funcall(mrb, fds, "delete_at", 1, mrb_fixnum_value(i));
        pollfds = (struct pollfd *) realloc(DATA_PTR(self), RARRAY_LEN(fds) * sizeof(struct pollfd));
        if (DATA_PTR(self) != pollfds) {
          for (mrb_int j = 0; j < RARRAY_LEN(fds); j++) {
            mrb_data_init(mrb_ary_ref(mrb, fds, j), &pollfds[j], &mrb_pollfd_type);
          }
          mrb_data_init(self, pollfds, &mrb_poll_type);
        }
        mrb_data_init(pollfd_obj, NULL, NULL);
        mrb_iv_remove(mrb, pollfd_obj, mrb_intern_lit(mrb, "socket"));
        break;
      }
    }
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
  mrb_assert(mrb_type(fds) == MRB_TT_ARRAY);

  int ret = poll((struct pollfd *) DATA_PTR(self), RARRAY_LEN(fds), timeout);

  if (unlikely(ret == -1)) {
    if (errno == EINTR) {
      return mrb_false_value();
    } else {
      mrb_sys_fail(mrb, "poll");
    }
  }
  else if (ret == 0) {
    return mrb_nil_value();
  }
  else {
    return mrb_fixnum_value(ret);
  }

  return mrb_fixnum_value(ret);
}

static mrb_value
mrb_pollfd_init(mrb_state *mrb, mrb_value self)
{
  if (unlikely(DATA_PTR(self))) {
    mrb_free(mrb, DATA_PTR(self));
  }

  mrb_data_init(self, NULL, NULL);

  mrb_value socket;

  mrb_get_args(mrb, "o", &socket);

  mrb_sym socket_sym = mrb_intern_lit(mrb, "socket");
  mrb_iv_set(mrb, self, socket_sym, socket);

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
  mrb_assert(DATA_PTR(self));

  mrb_value socket;

  mrb_get_args(mrb, "o", &socket);

  ((struct pollfd *) DATA_PTR(self))->fd = mrb_int(mrb, socket);

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
  mrb_assert(DATA_PTR(self));

  mrb_int events;

  mrb_get_args(mrb, "i", &events);

  if (unlikely(events < INT_MIN||events > INT_MAX)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "events doesn't fit into INT");
  }

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
  mrb_assert(DATA_PTR(self));

  mrb_int revents;

  mrb_get_args(mrb, "i", &revents);

  if (unlikely(revents < INT_MIN||revents > INT_MAX)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "revents doesn't fit into INT");
  }

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
  mrb_define_method(mrb, poll_class, "wait",        mrb_poll_wait, MRB_ARGS_OPT(1));

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

  mrb_define_const(mrb, mrb->kernel_module, "STDIN_FILENO", mrb_fixnum_value(STDIN_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDOUT_FILENO", mrb_fixnum_value(STDOUT_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDERR_FILENO", mrb_fixnum_value(STDERR_FILENO));
}

void mrb_mruby_poll_gem_final(mrb_state *mrb) {}
