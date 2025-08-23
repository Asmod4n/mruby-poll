#include "mrb_poll.hpp"

//-----------------------------------------------------------------------------
// 4. Internal FD descriptor
//-----------------------------------------------------------------------------
struct Fd {
  int   fd;
  short events;
  bool  marked_for_deletion = false;
};

struct PendingAdd {
  int   fd;
  short events;
};

struct Ensure {
  std::function<void()> func;

  Ensure(std::function<void()> f) : func(f) {}

  ~Ensure() { func(); }
};

class PollSet {
public:
  PollSet(size_t reserve_count = 16) {
    pollfds.reserve(reserve_count);
    fds.reserve(reserve_count);
    pending_adds.reserve(reserve_count);
  }

  // Enforce uniqueness. If called during wait, defer the insertion.
  void add(int raw_fd, short events, mrb_state* mrb) {
    // reject duplicates immediately (even during wait)
    for (auto& info : fds) {
      if (info.fd == raw_fd && !info.marked_for_deletion) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "fd is already registered");
        return; // not reached
      }
    }

    if (in_wait) {
      // queue it safely; no structural change to fds/pollfds
      try { pending_adds.push_back(PendingAdd{ raw_fd, events }); }
      catch (const std::bad_alloc&) { mrb_exc_raise(mrb, mrb_obj_value(mrb->nomem_err)); }
      need_rebuild = true;
      return;
    }

    try {
      fds.push_back(Fd{ raw_fd, events, false });
      pollfds.push_back(::pollfd{ raw_fd, events, 0 });
    } catch (const std::bad_alloc&) {
      mrb_exc_raise(mrb, mrb_obj_value(mrb->nomem_err));
    }
  }

  // Mark for deletion and delte when possible.
  void remove(int raw_fd) {
    for (auto& info : fds) {
      if (info.fd == raw_fd && !info.marked_for_deletion) {
        info.marked_for_deletion = true;
        need_rebuild = true;
        break;
      }
    }
    if (!in_wait) {
      compact();
    }
  }

  // Update interest mask; if in wait, defer applying to pollfds.
  // Returns true if updated, false if fd not found.
  bool update(int raw_fd, short events) {
    for (size_t i = 0; i < fds.size(); ++i) {
      if (fds[i].fd == raw_fd && !fds[i].marked_for_deletion) {
        fds[i].events = events;
        if (in_wait) {
          need_rebuild = true; // rebuild will resync pollfds
        } else {
          pollfds[i].events = events;
        }
        return true;
      }
    }
    return false;
  }

  // Remove/update/add in one call (events == 0 => remove).
  // Returns: -1 removed/not present, 0 added, 1 updated (for binding convenience).
  int remove_update_or_add(int raw_fd, short events, mrb_state* mrb) {
    if (events == 0) {
      remove(raw_fd);
      return -1;
    }
    // try update first
    if (update(raw_fd, events)) return 1;
    // otherwise add (will raise if somehow seen as duplicate)
    add(raw_fd, events, mrb);
    return 0;
  }

  // Clear entire set safely.
  void clear() {
    if (in_wait) {
      // mark all for deletion and drop any queued adds
      for (auto& info : fds) info.marked_for_deletion = true;
      pending_adds.clear();
      need_rebuild = true;
    } else {
      fds.clear();
      pollfds.clear();
    }
  }

  mrb_value wait(int timeout,
                 mrb_state* mrb,
                 mrb_value block,
                 void (*cb)(mrb_state*, mrb_value, int, short))
  {
    in_wait = true;
    Ensure cleanup([this] { compact(); in_wait = false; });

    int ret = ::poll(pollfds.data(), pollfds.size(), timeout);
    if (ret < 0) mrb_sys_fail(mrb, "poll");
    else if (ret == 0) return mrb_nil_value();

    for (size_t i = 0; i < pollfds.size(); ++i) {
      if (pollfds[i].revents != 0) {
        cb(mrb, block, fds[i].fd, pollfds[i].revents);
      }
    }
    return mrb_convert_number(mrb, ret);
  }

  mrb_value wait(int timeout, mrb_state* mrb) {
    in_wait = true;
    Ensure cleanup([this] { compact(); in_wait = false; });

    mrb_value result = mrb_nil_value();
    int ret = ::poll(pollfds.data(), pollfds.size(), timeout);
    if (ret > 0) {
      result = mrb_ary_new_capa(mrb, ret);
      mrb_int arena_index = mrb_gc_arena_save(mrb);
      for (size_t i = 0; i < pollfds.size(); ++i) {
        if (pollfds[i].revents != 0) {
          mrb_value pair = mrb_assoc_new(mrb, mrb_convert_number(mrb, fds[i].fd), mrb_convert_number(mrb, pollfds[i].revents));
          mrb_ary_push(mrb, result, pair);
          mrb_gc_arena_restore(mrb, arena_index);
        }
      }
    }
    if (ret < 0) mrb_sys_fail(mrb, "poll");
    return result;
  }

private:
  std::vector<Fd>             fds;
  std::vector<struct pollfd>  pollfds;

  // deferral state
  bool in_wait = false;
  bool need_rebuild = false;
  std::vector<PendingAdd> pending_adds;

  void compact() {
    if (!need_rebuild && pending_adds.empty()) return;

    // erase marked
    fds.erase(std::remove_if(fds.begin(), fds.end(),
                             [](const Fd& info){ return info.marked_for_deletion; }),
              fds.end());

    // apply queued adds (dedupe again in case state changed)
    for (auto& pa : pending_adds) {
      bool exists = false;
      for (auto& info : fds) {
        if (info.fd == pa.fd) { // if it reappeared, update instead
          info.events = pa.events;
          exists = true;
          break;
        }
      }
      if (!exists) fds.push_back(Fd{ pa.fd, pa.events, false });
    }
    pending_adds.clear();

    // rebuild pollfds to match fds and apply updated events
    pollfds.clear();
    pollfds.reserve(fds.size());
    for (auto& info : fds) {
      pollfds.push_back(::pollfd{ info.fd, info.events, 0 });
    }

    need_rebuild = false;
  }
};

//-----------------------------------------------------------------------------
// 6. mruby binding & lifecycle
//-----------------------------------------------------------------------------
MRB_CPP_DEFINE_TYPE(PollSet, PollSet)

static mrb_value
mrb_pollset_initialize(mrb_state* mrb, mrb_value self)
{
  mrb_int reserve = 16;
  mrb_get_args(mrb, "|i", &reserve);

  try {
    mrb_cpp_new<PollSet>(mrb, self,
                         static_cast<size_t>(reserve));
  }
  catch (const std::bad_alloc&) {
    mrb_exc_raise(mrb, mrb_obj_value(mrb->nomem_err));
  }
  return self;
}

static mrb_value
mrb_pollset_add(mrb_state* mrb, mrb_value self)
{
  mrb_int fd, events;
  mrb_get_args(mrb, "ii", &fd, &events);
  auto* ps = static_cast<PollSet*>(DATA_PTR(self));
  ps->add((int)fd, (short)events, mrb);
  return mrb_convert_number(mrb, fd); // return the fd
}

static mrb_value
mrb_pollset_update(mrb_state* mrb, mrb_value self)
{
  mrb_int fd, events;
  mrb_get_args(mrb, "ii", &fd, &events);
  auto* ps = static_cast<PollSet*>(DATA_PTR(self));
  bool ok = ps->update((int)fd, (short)events);
  return ok ? mrb_convert_number(mrb, fd) : mrb_nil_value(); // match old Ruby: fd or nil
}

static mrb_value
mrb_pollset_remove_update_or_add(mrb_state* mrb, mrb_value self)
{
  mrb_int fd, events;
  mrb_get_args(mrb, "ii", &fd, &events);
  auto* ps = static_cast<PollSet*>(DATA_PTR(self));

  int res = ps->remove_update_or_add((int)fd, (short)events, mrb);
  if (res == -1) return mrb_nil_value(); // removed
  return mrb_convert_number(mrb, fd);    // added or updated -> return fd
}

static mrb_value
mrb_pollset_remove(mrb_state* mrb, mrb_value self)
{
  mrb_int fd;
  mrb_get_args(mrb, "i", &fd);
  auto* ps = static_cast<PollSet*>(DATA_PTR(self));
  ps->remove(static_cast<int>(fd));
  return mrb_nil_value();
}

static mrb_value
mrb_pollset_clear(mrb_state* mrb, mrb_value self)
{
  auto* ps = static_cast<PollSet*>(DATA_PTR(self));
  ps->clear();
  return self; // return self, like the Ruby version
}

static mrb_value
mrb_pollset_wait(mrb_state* mrb, mrb_value self)
{
  mrb_int timeout = -1;
  mrb_value block = mrb_nil_value();
  mrb_get_args(mrb, "|i&", &timeout, &block);

  auto* ps = static_cast<PollSet*>(DATA_PTR(self));
  if (!mrb_nil_p(block)) {
    if (mrb_type(block) != MRB_TT_PROC) {
      mrb_raise(mrb, E_TYPE_ERROR, "not a block");
    }

    auto callback = [](mrb_state* m, mrb_value blk, int fd, short revents) {
      mrb_value argv[2] = {
        mrb_convert_number(m, fd),
        mrb_convert_number(m, revents)
      };
      mrb_yield_argv(m, blk, 2, argv);
    };

    return ps->wait(static_cast<int>(timeout), mrb, block, callback);
  } else {
    return ps->wait(static_cast<int>(timeout), mrb);
  }
}

// Gem initialization
MRB_BEGIN_DECL
void
mrb_mruby_poll_gem_init(mrb_state* mrb)
{
  auto poll_class = mrb_define_class(mrb, "Poll", mrb->object_class);
  MRB_SET_INSTANCE_TT(poll_class, MRB_TT_DATA);

  mrb_define_method(mrb, poll_class, "initialize", mrb_pollset_initialize, MRB_ARGS_OPT(1));
  mrb_define_method(mrb, poll_class, "add",        mrb_pollset_add,        MRB_ARGS_REQ(2));
  mrb_define_method(mrb, poll_class, "update",     mrb_pollset_update,     MRB_ARGS_REQ(2));
  mrb_define_method(mrb, poll_class, "remove",     mrb_pollset_remove,     MRB_ARGS_REQ(1));
  mrb_define_method(mrb, poll_class, "remove_update_or_add",     mrb_pollset_remove_update_or_add,     MRB_ARGS_REQ(2));
  mrb_define_method(mrb, poll_class, "clear",        mrb_pollset_clear,        MRB_ARGS_NONE());
  mrb_define_method(mrb, poll_class, "wait",       mrb_pollset_wait,       MRB_ARGS_OPT(1) | MRB_ARGS_BLOCK());
  mrb_define_const(mrb, poll_class, "Err", mrb_convert_number(mrb, POLLERR));
  mrb_define_const(mrb, poll_class, "Hup", mrb_convert_number(mrb, POLLHUP));
  mrb_define_const(mrb, poll_class, "In", mrb_convert_number(mrb, POLLIN));
  mrb_define_const(mrb, poll_class, "Nval", mrb_convert_number(mrb, POLLNVAL));
  mrb_define_const(mrb, poll_class, "Out", mrb_convert_number(mrb, POLLOUT));
  mrb_define_const(mrb, poll_class, "Pri", mrb_convert_number(mrb, POLLPRI));


  mrb_define_const(mrb, mrb->kernel_module, "STDIN_FILENO", mrb_convert_number(mrb, STDIN_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDOUT_FILENO", mrb_convert_number(mrb, STDOUT_FILENO));
  mrb_define_const(mrb, mrb->kernel_module, "STDERR_FILENO", mrb_convert_number(mrb, STDERR_FILENO));
}

void
mrb_mruby_poll_gem_final(mrb_state*) {}
MRB_END_DECL
