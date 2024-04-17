#include "mrb_poll.h"
#include <alloca.h>

#if (__GNUC__ >= 3) || (__INTEL_COMPILER >= 800) || defined(__clang__)
# define likely(x) __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x) (x)
# define unlikely(x) (x)
#endif

struct mrb_pollset_data {
  mrb_int hash_pos;
  struct pollfd **pollfds;
};

static int
mrb_poll_create_pollset(mrb_state *mrb, mrb_value key, mrb_value val, void *data)
{
  struct mrb_pollset_data *userdata = data;
  mrb_value socket = mrb_iv_get(mrb, val, mrb_intern_lit(mrb, "@socket"));
  struct pollfd *pollfd = userdata->pollfds[userdata->hash_pos++];
  pollfd->fd = (int) mrb_fixnum(mrb_convert_type(mrb, socket, MRB_TT_INTEGER, "Integer", "fileno"));
  pollfd->events = (short) mrb_as_int(mrb, mrb_iv_get(mrb, val, mrb_intern_lit(mrb, "@events")));
  return 0;
}

static int
mrb_poll_update_fds(mrb_state *mrb, mrb_value key, mrb_value val, void *data)
{
  struct mrb_pollset_data *userdata = data;
  struct pollfd *pollfd = userdata->pollfds[userdata->hash_pos++];
  mrb_iv_set(mrb, val, mrb_intern_lit(mrb, "@revents"), mrb_fixnum_value(pollfd->revents));
  return 0;
}

static mrb_value
mrb_poll_wait(mrb_state *mrb, mrb_value self)
{
  mrb_value fds;
  mrb_int timeout = -1;
  mrb_value block = mrb_nil_value();

  int ret;

  mrb_get_args(mrb, "|i&", &timeout, &block);

  mrb_assert(timeout >= INT_MIN&&timeout <= INT_MAX);

  fds = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@fds"));
  if (!mrb_hash_p(fds)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "corrupted fds");
  }
  mrb_int fds_size = mrb_hash_size(mrb, fds);

  if (!(fds_size > 0 && sizeof(struct pollfd) > 0 &&
      fds_size <= SIZE_MAX / sizeof(struct pollfd))) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "too many fds");
      }

  struct pollfd *pollfds = mrb_malloc(mrb, fds_size * sizeof(struct pollfd));

  struct mrb_pollset_data data = {
    .hash_pos = 0,
    .pollfds = &pollfds
  };
  mrb_hash_foreach(mrb, mrb_hash_ptr(fds), mrb_poll_create_pollset, &data);

  errno = 0;
  ret = poll(pollfds, fds_size, (int) timeout);

  struct mrb_jmpbuf* prev_jmp = mrb->jmp;
  struct mrb_jmpbuf c_jmp;
  mrb_value ret_val = self;

  MRB_TRY(&c_jmp)
  {
    mrb->jmp = &c_jmp;

    if (ret > 0) {
      data.hash_pos = 0;
      mrb_hash_foreach(mrb, mrb_hash_ptr(fds), mrb_poll_update_fds, &data);
      int idx = mrb_gc_arena_save(mrb);
      if (mrb_type(block) == MRB_TT_PROC) {
        for (mrb_int i = 0; i < fds_size; i++) {
          if (pollfds[i].revents) {
            mrb_yield(mrb, block, mrb_hash_get(mrb, fds, mrb_fixnum_value(pollfds[i].fd)));
            mrb_gc_arena_restore(mrb, idx);
          }
        }
      } else {
        ret_val = mrb_ary_new_capa(mrb, ret);
        for (mrb_int i = 0; i < fds_size; i++) {
          if (pollfds[i].revents) {
            mrb_ary_push(mrb, ret_val, mrb_hash_get(mrb, fds, mrb_fixnum_value(pollfds[i].fd)));
          }
        }
      }
    }
    else if (ret == 0) {
      ret_val = mrb_nil_value();
    }
    else if (unlikely(ret == -1)) {
      if (errno == EINTR) {
        ret_val = mrb_false_value();
      } else {
        mrb_sys_fail(mrb, "poll");
      }
    }
    else {
      mrb_raise(mrb, E_RUNTIME_ERROR, "poll returned erroneous value");
    }

    mrb_free(mrb, pollfds);

    mrb->jmp = prev_jmp;
  }
  MRB_CATCH(&c_jmp)
  {
    mrb->jmp = prev_jmp;
    mrb_free(mrb, pollfds);
    MRB_THROW(mrb->jmp);
  }
  MRB_END_EXC(&c_jmp);

  return ret_val;
}

void
mrb_mruby_poll_gem_init(mrb_state *mrb)
{
  struct RClass *poll_class;
  poll_class = mrb_define_class(mrb, "Poll", mrb->object_class);
  mrb_define_const(mrb, poll_class, "Err", mrb_fixnum_value(POLLERR));
  mrb_define_const(mrb, poll_class, "Hup", mrb_fixnum_value(POLLHUP));
  mrb_define_const(mrb, poll_class, "In", mrb_fixnum_value(POLLIN));
  mrb_define_const(mrb, poll_class, "Nval", mrb_fixnum_value(POLLNVAL));
  mrb_define_const(mrb, poll_class, "Out", mrb_fixnum_value(POLLOUT));
  mrb_define_const(mrb, poll_class, "Pri", mrb_fixnum_value(POLLPRI));

  mrb_define_method(mrb, poll_class, "wait", mrb_poll_wait, MRB_ARGS_OPT(1)|MRB_ARGS_BLOCK());

  mrb_define_const(mrb, mrb->kernel_module, "STDIN_FILENO", mrb_fixnum_value(STDIN_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDOUT_FILENO", mrb_fixnum_value(STDOUT_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDERR_FILENO", mrb_fixnum_value(STDERR_FILENO));
}

void mrb_mruby_poll_gem_final(mrb_state *mrb) {}
