// mruby-poll.cpp
// Implements:
//   class Poll
//     - add(socket, events = Poll::In) -> Poll::Fd
//     - remove(fd_or_handle) -> nil
//     - wait(timeout_ms = -1) { |Poll::Fd| ... } -> self
//       or without block -> Array< Poll::Fd >
//       returns false on EINTR, nil on timeout
//   class Poll::Fd
//     - socket / socket=
//     - events / events=
//     - revents / revents=
//     - readable? / writable? / err? / disconnected?
//
// Semantics:
// - One entry per numeric fd (add raises ArgumentError if duplicate).
// - Safe to call remove/update inside wait: changes are applied in compact() after wait finishes.
// - When not in a wait, events= syncs to pollfds immediately.
// - wait yields/returns Poll::Fd objects (same instances created by add).
//
// Requires mruby and mruby-io (for fileno/to_i if you pass IO/Socket).
// On Windows, you’ll likely need a separate WSAPoll path; this is POSIX poll.

#include "mrb_poll.h"
#include "mruby/value.h"

#include <poll.h>
#include <errno.h>
#include <string.h>

struct PollSet {
  struct pollfd *fds;
  size_t pollfd_size;
  size_t pollfd_capa;
  mrb_value fds_array;         // Array<Poll::Fd>
  mrb_value clear_list_array;  // unused in this minimal impl, but kept for API symmetry
};

static void
pollset_free(mrb_state *mrb, void *p)
{
  struct PollSet *pollset = (struct PollSet *) p;
  if (pollset) {
    mrb_free(mrb, pollset->fds);
    mrb_free(mrb, p);
  }
}
static const struct mrb_data_type pollset_type = {"PollSet", pollset_free};

struct Pfd {
  struct pollfd *fd;             // pointer into PollSet->fds[pollfd_index]
  mrb_int pollfd_index;          // index within PollSet->fds and fds_array
  mrb_bool marked_for_deletion;  // true if scheduled for removal after wait
};
static const struct mrb_data_type pollfd_type = {"Pfd", mrb_free};

/* helpers */

static inline struct PollSet*
get_pollset(mrb_state *mrb, mrb_value self)
{
  return DATA_CHECK_GET_PTR(mrb, self, &pollset_type, struct PollSet);
}

static inline struct Pfd*
get_pfd(mrb_state *mrb, mrb_value self)
{
  return DATA_CHECK_GET_PTR(mrb, self, &pollfd_type, struct Pfd);
}

static int
value_to_fd(mrb_state *mrb, mrb_value sock)
{
  if (mrb_integer_p(sock)) {
    return (int)mrb_integer(sock);
  }
  // Try fileno
  if (mrb_respond_to(mrb, sock, MRB_SYM(fileno))) {
    mrb_value v = mrb_funcall_argv(mrb, sock, MRB_SYM(fileno), 0, NULL);
    if (mrb_integer_p(v)) return (int)mrb_integer(v);
  }
  // Fallback to to_i
  if (mrb_respond_to(mrb, sock, MRB_SYM(to_i))) {
    mrb_value v = mrb_funcall_argv(mrb, sock, MRB_SYM(to_i), 0, NULL);
    if (mrb_integer_p(v)) return (int)mrb_integer(v);
  }
  mrb_raise(mrb, E_TYPE_ERROR, "socket must be an Integer or respond to #fileno/#to_i");
  return -1;
}

static void
ensure_capacity(mrb_state *mrb, struct PollSet *pollset, size_t need)
{
  if (need <= pollset->pollfd_capa) return;
  size_t newc = pollset->pollfd_capa;
  while (newc < need) newc *= 2;
  pollset->fds = (struct pollfd*)mrb_realloc(mrb, pollset->fds, sizeof(struct pollfd) * newc);
  pollset->pollfd_capa = newc;

  // sync all Poll::Fd pointers after realloc
  mrb_int n = (mrb_int)pollset->pollfd_size;
  for (mrb_int i = 0; i < n; ++i) {
    mrb_value fd_obj = mrb_ary_ref(mrb, pollset->fds_array, i);
    if (mrb_type(fd_obj) == MRB_TT_DATA) {
      struct Pfd *p = get_pfd(mrb, fd_obj);
      if (p) {
        p->fd = &pollset->fds[i];
        p->pollfd_index = i;
      }
    }
  }
}

static mrb_int
find_index_by_fd(struct PollSet *pollset, int fd)
{
  for (mrb_int i = 0; i < (mrb_int)pollset->pollfd_size; ++i) {
    if (pollset->fds[i].fd == fd) return i;
  }
  return -1;
}

static void
sync_fd_pointer_for(struct PollSet *pollset, mrb_state *mrb, mrb_int idx)
{
  mrb_value fd_obj = mrb_ary_ref(mrb, pollset->fds_array, idx);
  struct Pfd *p = get_pfd(mrb, fd_obj);
  if (p) {
    p->fd = &pollset->fds[idx];
    p->pollfd_index = idx;
  }
}

static void
compact_pollset(mrb_state *mrb, mrb_value poll_obj, struct PollSet *pollset)
{
  // Rebuild arrays skipping marked entries
  mrb_int n = (mrb_int)pollset->pollfd_size;
  mrb_int write_i = 0;
  for (mrb_int read_i = 0; read_i < n; ++read_i) {
    mrb_value fd_obj = mrb_ary_ref(mrb, pollset->fds_array, read_i);
    struct Pfd *p = get_pfd(mrb, fd_obj);
    if (p && !p->marked_for_deletion) {
      if (write_i != read_i) {
        // move pollfd
        pollset->fds[write_i] = pollset->fds[read_i];
        // move fd_obj in Ruby array
        mrb_ary_set(mrb, pollset->fds_array, write_i, fd_obj);
      }
      // update pointer/index
      p->fd = &pollset->fds[write_i];
      p->pollfd_index = write_i;
      write_i++;
    }
  }
  // shrink Ruby array length
  while (RARRAY_LEN(pollset->fds_array) > (mrb_int)write_i) {
    mrb_ary_pop(mrb, pollset->fds_array);
  }
  pollset->pollfd_size = (size_t)write_i;

  // clear clear_list_array if used
  if (!mrb_nil_p(pollset->clear_list_array)) {
    mrb_ary_clear(mrb, pollset->clear_list_array);
  }

  // clear @in_wait flag if any (caller will usually handle)
  (void)poll_obj;
}

static mrb_bool
in_wait_p(mrb_state *mrb, mrb_value self)
{
  mrb_value v = mrb_iv_get(mrb, self, MRB_SYM(in_wait));
  return mrb_test(v);
}

/* Poll::initialize */

static mrb_value
mrb_pollset_initialize(mrb_state *mrb, mrb_value self)
{
  struct PollSet *pollset = (struct PollSet *) mrb_realloc(mrb, DATA_PTR(self), sizeof(*pollset));
  mrb_data_init(self, pollset, &pollset_type);
  pollset->fds = (struct pollfd *) mrb_malloc(mrb, sizeof(struct pollfd) * 8);
  pollset->pollfd_size = 0;
  pollset->pollfd_capa = 8;
  pollset->fds_array = mrb_ary_new_capa(mrb, 8);
  mrb_gc_protect(mrb, pollset->fds_array);
  mrb_iv_set(mrb, self, MRB_SYM(fds), pollset->fds_array);
  pollset->clear_list_array = mrb_ary_new(mrb);
  mrb_gc_protect(mrb, pollset->clear_list_array);
  mrb_iv_set(mrb, self, MRB_SYM(clear_list), pollset->clear_list_array);
  mrb_iv_set(mrb, self, MRB_SYM(in_wait), mrb_false_value());
  return self;
}

/* Poll::Fd#initialize(pollset_obj, index, cptr_to_pollfd, socket_obj) */

// args: pollset_obj, pollfd_index, fd_cptr, socket_obj, socket_fd
static mrb_value
mrb_pollfd_initialize(mrb_state *mrb, mrb_value self)
{
  mrb_value pollset_obj, fd_cptr, socket_obj;
  mrb_int pollfd_index;

  mrb_get_args(mrb, "oioo", &pollset_obj, &pollfd_index, &fd_cptr, &socket_obj);

  DATA_CHECK_GET_PTR(mrb, pollset_obj, &pollset_type, struct PollSet);
  mrb_iv_set(mrb, self, MRB_SYM(pollset), pollset_obj);

  struct pollfd *pfd = NULL;
  if (mrb_cptr_p(fd_cptr)) {
    pfd = (struct pollfd *) mrb_cptr(fd_cptr);
  } else {
    mrb_raise(mrb, E_TYPE_ERROR, "third argument is not a cptr");
  }

  struct Pfd *pollfd = (struct Pfd *) mrb_realloc(mrb, DATA_PTR(self), sizeof(*pollfd));
  mrb_data_init(self, pollfd, &pollfd_type);
  pollfd->fd = pfd;
  pollfd->pollfd_index = pollfd_index;
  pollfd->marked_for_deletion = FALSE;

  // keep Ruby socket alive
  mrb_iv_set(mrb, self, MRB_IVSYM(socket), socket_obj);


  return self;
}



/* Poll#add(socket, events = POLLIN) -> Poll::Fd */

static mrb_value
mrb_poll_add(mrb_state *mrb, mrb_value self)
{
  struct PollSet *pollset = get_pollset(mrb, self);
  mrb_value socket_v;
  mrb_int events = POLLIN;
  mrb_get_args(mrb, "o|i", &socket_v, &events);

  int fdnum = value_to_fd(mrb, socket_v);
  if (find_index_by_fd(pollset, fdnum) >= 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "duplicate fd");
  }

  // grow capacity if needed
  ensure_capacity(mrb, pollset, pollset->pollfd_size + 1);

  // append pollfd
  size_t idx = pollset->pollfd_size;
  pollset->fds[idx].fd = fdnum;
  pollset->fds[idx].events = (short)events;
  pollset->fds[idx].revents = 0;

  // build Poll::Fd object
  struct RClass* pollfd_cls = mrb_class_get_under_id(mrb, mrb_class(mrb, self), MRB_SYM(_Fd));
  mrb_value args[4];
  args[0] = self;                                      // pollset_obj
  args[1] = mrb_fixnum_value(idx);                       // pollfd_index
  args[2] = mrb_cptr_value(mrb, &pollset->fds[idx]);     // pointer to struct pollfd
  args[3] = socket_v;

  mrb_value fd_obj = mrb_obj_new(mrb, pollfd_cls, 4, args);
  mrb_gc_protect(mrb, fd_obj);

  // Set socket ivar for convenience/consistency

  // store in array (aligned with pollfd)
  mrb_ary_push(mrb, pollset->fds_array, fd_obj);

  pollset->pollfd_size++;

  return fd_obj;
}

/* Poll#update(socket_int, events) -> Integer or nil */

static mrb_value
mrb_poll_update(mrb_state *mrb, mrb_value self)
{
  struct PollSet *pollset = get_pollset(mrb, self);
  mrb_value socket_v;
  mrb_int events;
  mrb_get_args(mrb, "oi", &socket_v, &events);
  int fdnum = value_to_fd(mrb, socket_v);

  mrb_int idx = find_index_by_fd(pollset, fdnum);
  if (idx < 0) return mrb_nil_value();

  pollset->fds[idx].events = (short)events;
  sync_fd_pointer_for(pollset, mrb, idx);
  return mrb_fixnum_value(fdnum);
}

/* Poll#clear -> self */

static mrb_value
mrb_poll_clear(mrb_state *mrb, mrb_value self)
{
  struct PollSet *pollset = get_pollset(mrb, self);
  pollset->pollfd_size = 0;
  // clear Ruby arrays
  mrb_ary_clear(mrb, pollset->fds_array);
  mrb_ary_clear(mrb, pollset->clear_list_array);
  return self;
}

/* Poll#remove(fd_or_handle) -> nil */

static mrb_value
mrb_poll_remove(mrb_state *mrb, mrb_value self)
{
  struct PollSet *pollset = get_pollset(mrb, self);
  mrb_value arg;
  mrb_get_args(mrb, "o", &arg);


  mrb_int idx = -1;
  if (mrb_type(arg) == MRB_TT_DATA) {
    // Might be a Poll::Fd
    struct Pfd *p = get_pfd(mrb, arg);
    if (p) {
      // validate that this object still maps to the same slot
      if (p->pollfd_index >= 0 && p->pollfd_index < (mrb_int)pollset->pollfd_size) {
        mrb_value cur = mrb_ary_ref(mrb, pollset->fds_array, p->pollfd_index);
        if (mrb_obj_eq(mrb, cur, arg)) {
          idx = p->pollfd_index;
        }
      }
      if (idx < 0) {
        // fallback by fd number
        idx = find_index_by_fd(pollset, p->fd ? p->fd->fd : -1);
      }
    }
  } else {
    int fdnum = value_to_fd(mrb, arg);
    idx = find_index_by_fd(pollset, fdnum);
  }

  if (idx < 0) return mrb_nil_value(); // no-op

  // mark for deletion
  mrb_value fd_obj = mrb_ary_ref(mrb, pollset->fds_array, idx);
  struct Pfd *p = get_pfd(mrb, fd_obj);
  if (p) p->marked_for_deletion = TRUE;

  if (!in_wait_p(mrb, self)) {
    // compact immediately when not in wait
    compact_pollset(mrb, self, pollset);
  }
  return mrb_nil_value();
}

/* Poll#remove_update_or_add(fd, events) -> nil or Poll::Fd */

static mrb_value
mrb_poll_remove_update_or_add(mrb_state *mrb, mrb_value self)
{
  struct PollSet *pollset = get_pollset(mrb, self);
  mrb_value socket_v;
  mrb_int events;
  mrb_get_args(mrb, "oi", &socket_v, &events);
  int fdnum = value_to_fd(mrb, socket_v);

  if (events == 0) {
    // remove path
    // We need to actually remove; call internal logic:
    mrb_int idx = find_index_by_fd(pollset, fdnum);
    if (idx >= 0) {
      mrb_value fd_obj = mrb_ary_ref(mrb, pollset->fds_array, idx);
      struct Pfd *p = get_pfd(mrb, fd_obj);
      if (p) p->marked_for_deletion = TRUE;
      if (!in_wait_p(mrb, self)) {
        compact_pollset(mrb, self, pollset);
      }
    }
    return mrb_nil_value();
  }

  mrb_int idx = find_index_by_fd(pollset, fdnum);
  if (idx >= 0) {
    // update path, return existing Poll::Fd
    pollset->fds[idx].events = (short)events;
    sync_fd_pointer_for(pollset, mrb, idx);
    return mrb_ary_ref(mrb, pollset->fds_array, idx);
  }

  // add path
  // mrb_poll_add consumed the same arguments pattern? It expects (socket, [events])
  // But since we called directly here, re-construct:
  // Instead, do manual add to avoid re-parsing args:
  // We'll implement add inline if necessary; above call won't work correctly,
  // so replace with manual:

  // Manual add:
  ensure_capacity(mrb, pollset, pollset->pollfd_size + 1);
  size_t new_idx = pollset->pollfd_size;
  pollset->fds[new_idx].fd = fdnum;
  pollset->fds[new_idx].events = (short)events;
  pollset->fds[new_idx].revents = 0;

  struct RClass* pollfd_cls = mrb_class_get_under_id(mrb, mrb_class(mrb, self), MRB_SYM(_Fd));
  mrb_value args[4];
  args[0] = self;
  args[1] = mrb_fixnum_value((mrb_int)new_idx);
  args[2] = mrb_cptr_value(mrb, &pollset->fds[new_idx]);
  args[3] = socket_v;
  mrb_value fd_obj = mrb_obj_new(mrb, pollfd_cls, 4, args);
  mrb_gc_protect(mrb, fd_obj);
  mrb_ary_push(mrb, pollset->fds_array, fd_obj);
  pollset->pollfd_size++;

  return fd_obj;
}

/* Poll#wait(timeout_ms = -1) { |fd| ... } -> self, or -> Array<Poll::Fd>, or false/nil */

static mrb_value
mrb_poll_wait(mrb_state *mrb, mrb_value self)
{
  struct PollSet *pollset = get_pollset(mrb, self);
  mrb_int timeout = -1;
  mrb_value blk = mrb_nil_value();
  mrb_get_args(mrb, "|i&", &timeout, &blk);

  // clear previous revents
  for (size_t i = 0; i < pollset->pollfd_size; ++i) {
    pollset->fds[i].revents = 0;
  }

  mrb_iv_set(mrb, self, MRB_SYM(in_wait), mrb_true_value());


  int ret = poll(pollset->fds, (nfds_t)pollset->pollfd_size, (int)timeout);

  if (ret < 0) {
    int e = errno;
    mrb_iv_set(mrb, self, MRB_SYM(in_wait), mrb_false_value());
    if (e == EINTR) {
      compact_pollset(mrb, self, pollset);
      return mrb_false_value();
    }
    mrb_sys_fail(mrb, "poll");
  }

  if (ret == 0) {
    mrb_iv_set(mrb, self, MRB_SYM(in_wait), mrb_false_value());
    compact_pollset(mrb, self, pollset);
    return mrb_nil_value();
  }

  // either yield or collect
  if (!mrb_nil_p(blk)) {
    // yield each ready fd directly
    for (size_t i = 0;  i < pollset->pollfd_size; i++) {
      if (pollset->fds[i].revents != 0) {
        mrb_value fd_obj = mrb_ary_ref(mrb, pollset->fds_array, i);
        sync_fd_pointer_for(pollset, mrb, i);
        mrb_yield_argv(mrb, blk, 1, &fd_obj);
      }
    }
    mrb_iv_set(mrb, self, MRB_SYM(in_wait), mrb_false_value());
    compact_pollset(mrb, self, pollset);
    return self;
  } else {
    // build and return array
    mrb_value ready_ary = mrb_ary_new_capa(mrb, ret);
    mrb_gc_protect(mrb, ready_ary);
    int idx = mrb_gc_arena_save(mrb);
    for (size_t i = 0;  i < pollset->pollfd_size; i++) {
      if (pollset->fds[i].revents != 0) {
        mrb_value fd_obj = mrb_ary_ref(mrb, pollset->fds_array, i);
        sync_fd_pointer_for(pollset, mrb, i);
        mrb_ary_push(mrb, ready_ary, fd_obj);
        mrb_gc_arena_restore(mrb, idx);
      }
    }
    mrb_iv_set(mrb, self, MRB_SYM(in_wait), mrb_false_value());
    compact_pollset(mrb, self, pollset);
    return ready_ary;
  }
}


/* Poll::Fd attribute methods */

static mrb_value
mrb_pollfd_socket_get(mrb_state *mrb, mrb_value self)
{
  return mrb_iv_get(mrb, self, MRB_IVSYM(socket));
}

static mrb_value
mrb_pollfd_socket_set(mrb_state *mrb, mrb_value self)
{
  struct Pfd *p = get_pfd(mrb, self);
  if (!p || !p->fd) return mrb_nil_value();
  mrb_value v;
  mrb_get_args(mrb, "o", &v);
  int fdnum = value_to_fd(mrb, v);
  p->fd->fd = fdnum;
  mrb_iv_set(mrb, self, MRB_IVSYM(socket), v);
  return v;
}

static mrb_value
mrb_pollfd_events_get(mrb_state *mrb, mrb_value self)
{
  struct Pfd *p = get_pfd(mrb, self);
  if (!p || !p->fd) return mrb_fixnum_value(0);
  return mrb_fixnum_value(p->fd->events);
}

static mrb_value
mrb_pollfd_events_set(mrb_state *mrb, mrb_value self)
{
  struct Pfd *p = get_pfd(mrb, self);
  mrb_int ev;
  mrb_get_args(mrb, "i", &ev);
  if (p && p->fd) {
    p->fd->events = (short)ev;
  }
  return mrb_fixnum_value(ev);
}

static mrb_value
mrb_pollfd_revents_get(mrb_state *mrb, mrb_value self)
{
  struct Pfd *p = get_pfd(mrb, self);
  if (!p || !p->fd) return mrb_fixnum_value(0);
  return mrb_fixnum_value(p->fd->revents);
}

static mrb_value
mrb_pollfd_revents_set(mrb_state *mrb, mrb_value self)
{
  struct Pfd *p = get_pfd(mrb, self);
  mrb_int ev;
  mrb_get_args(mrb, "i", &ev);
  if (p && p->fd) {
    p->fd->revents = (short)ev;
  }
  return mrb_fixnum_value(ev);
}

static mrb_value
mrb_pollfd_readable(mrb_state *mrb, mrb_value self)
{
  struct Pfd *p = get_pfd(mrb, self);
  if (!p || !p->fd) return mrb_false_value();
  return mrb_bool_value((p->fd->revents & POLLIN) != 0);
}

static mrb_value
mrb_pollfd_writable(mrb_state *mrb, mrb_value self)
{
  struct Pfd *p = get_pfd(mrb, self);
  if (!p || !p->fd) return mrb_false_value();
  return mrb_bool_value((p->fd->revents & POLLOUT) != 0);
}

static mrb_value
mrb_pollfd_err(mrb_state *mrb, mrb_value self)
{
  struct Pfd *p = get_pfd(mrb, self);
  if (!p || !p->fd) return mrb_false_value();
  return mrb_bool_value((p->fd->revents & (POLLERR | POLLNVAL)) != 0);
}

static mrb_value
mrb_pollfd_disconnected(mrb_state *mrb, mrb_value self)
{
  struct Pfd *p = get_pfd(mrb, self);
  if (!p || !p->fd) return mrb_false_value();
  return mrb_bool_value((p->fd->revents & POLLHUP) != 0);
}

/* Gem initialization */
MRB_BEGIN_DECL
void mrb_mruby_poll_gem_init(mrb_state* mrb) {
  struct RClass* poll_cls   = mrb_define_class(mrb, "Poll", mrb->object_class);
  MRB_SET_INSTANCE_TT(poll_cls, MRB_TT_DATA);

  // Error/event constants
  mrb_define_const(mrb, poll_cls, "Err",  mrb_fixnum_value(POLLERR));
  mrb_define_const(mrb, poll_cls, "Hup",  mrb_fixnum_value(POLLHUP));
  mrb_define_const(mrb, poll_cls, "In",   mrb_fixnum_value(POLLIN));
  mrb_define_const(mrb, poll_cls, "NVal", mrb_fixnum_value(POLLNVAL));
  mrb_define_const(mrb, poll_cls, "Out",  mrb_fixnum_value(POLLOUT));
  mrb_define_const(mrb, poll_cls, "Pri",  mrb_fixnum_value(POLLPRI));

  // Poll methods
  mrb_undef_method(mrb, poll_cls, "initialize_copy");
  mrb_define_method(mrb, poll_cls, "initialize", mrb_pollset_initialize, MRB_ARGS_NONE());
  mrb_define_method(mrb, poll_cls, "add",        mrb_poll_add,        MRB_ARGS_ARG(1,1)); // socket, [events]
  mrb_define_method(mrb, poll_cls, "update",     mrb_poll_update,     MRB_ARGS_REQ(2));
  mrb_define_method(mrb, poll_cls, "clear",      mrb_poll_clear,      MRB_ARGS_NONE());
  mrb_define_method(mrb, poll_cls, "remove",     mrb_poll_remove,     MRB_ARGS_REQ(1));
  mrb_define_method(mrb, poll_cls, "remove_update_or_add", mrb_poll_remove_update_or_add, MRB_ARGS_REQ(2));
  mrb_define_method(mrb, poll_cls, "wait",       mrb_poll_wait,       MRB_ARGS_OPT(1)|MRB_ARGS_BLOCK());

  // Nested class Poll::Fd
  struct RClass* pollfd_cls = mrb_define_class_under(mrb, poll_cls, "_Fd", mrb->object_class);
  MRB_SET_INSTANCE_TT(pollfd_cls, MRB_TT_DATA);

  mrb_define_method(mrb, pollfd_cls, "initialize", mrb_pollfd_initialize, MRB_ARGS_REQ(5));

  mrb_define_method(mrb, pollfd_cls, "socket",        mrb_pollfd_socket_get, MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_cls, "socket=",       mrb_pollfd_socket_set, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, pollfd_cls, "events",        mrb_pollfd_events_get, MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_cls, "events=",       mrb_pollfd_events_set, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, pollfd_cls, "revents",       mrb_pollfd_revents_get, MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_cls, "revents=",      mrb_pollfd_revents_set, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, pollfd_cls, "readable?",     mrb_pollfd_readable,    MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_cls, "writable?",     mrb_pollfd_writable,    MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_cls, "err?",          mrb_pollfd_err,         MRB_ARGS_NONE());
  mrb_define_method(mrb, pollfd_cls, "disconnected?", mrb_pollfd_disconnected,MRB_ARGS_NONE());

  mrb_define_const(mrb, mrb->kernel_module, "STDIN_FILENO",  mrb_convert_number(mrb, STDIN_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDOUT_FILENO", mrb_convert_number(mrb, STDOUT_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDERR_FILENO", mrb_convert_number(mrb, STDERR_FILENO));
}

void mrb_mruby_poll_gem_final(mrb_state* mrb) {
  /* nothing */
}
MRB_END_DECL