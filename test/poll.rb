assert("Poll basic add/remove and timeout") do
  poll = Poll.new
  fd_obj = poll.add(STDOUT_FILENO, Poll::Out)
  assert_equal STDOUT_FILENO, fd_obj.socket
  poll.remove(fd_obj) # no raise

  # Removing twice is a no-op
  poll.remove(fd_obj)

  # Empty set times out and returns nil
  assert_nil poll.wait(10)
end

assert("Poll rejects duplicate add") do
  poll = Poll.new
  poll.add(STDOUT_FILENO, Poll::Out)
  assert_raise(ArgumentError) { poll.add(STDOUT_FILENO, Poll::Out) }
end

assert("Poll update existing/non-existing") do
  poll = Poll.new
  fd_obj = poll.add(STDOUT_FILENO, Poll::Out)
  # update existing
  ret = poll.update(fd_obj.socket, Poll::Out)
  assert_equal fd_obj.socket, ret
  # update non-existing
  assert_nil poll.update(STDIN_FILENO, Poll::In)
end

assert("Poll remove_update_or_add: remove, update, add") do
  poll = Poll.new
  fd_obj = poll.add(STDOUT_FILENO, Poll::Out)

  # remove path
  assert_nil poll.remove_update_or_add(fd_obj.socket, 0)
  assert_nil poll.update(fd_obj.socket, Poll::Out)

  # add path
  ret = poll.remove_update_or_add(STDOUT_FILENO, Poll::Out)
  assert_equal STDOUT_FILENO, ret.socket

  # update path
  ret2 = poll.remove_update_or_add(STDOUT_FILENO, Poll::Out)
  assert_equal STDOUT_FILENO, ret2.socket
end

assert("Poll clear returns self and empties set") do
  poll = Poll.new
  fd1 = poll.add(STDOUT_FILENO, Poll::Out)
  fd2 = poll.add(STDIN_FILENO,  Poll::In)
  assert_equal poll, poll.clear
  assert_nil poll.update(STDOUT_FILENO, Poll::Out)
  assert_nil poll.update(STDIN_FILENO,  Poll::In)
end

assert("Remove inside wait using a pipe") do
  r, w = IO.pipe
  begin
    poll = Poll.new
    fd_obj = poll.add(r, Poll::In)

    # Make r readable
    w.write("x")

    called = false
    poll.wait(100) do |ready_fd|
      called = true
      assert_equal r, ready_fd.socket
      assert_equal Poll::In, ready_fd.revents
      poll.remove(fd_obj) # safe inside callback
    end
    assert_true called

    # After wait/compact, r is gone
    assert_nil poll.update(r, Poll::In)
  ensure
    r.close rescue nil
    w.close rescue nil
  end
end

assert("Only the written pipe fires") do
  r1, w1 = IO.pipe
  r2, w2 = IO.pipe
  begin
    poll = Poll.new
    poll.add(r1)
    poll.add(r2)

    # Make only r2 readable
    w2.write("y")

    seen = []
    ret = poll.wait() {|pollfd| seen << pollfd}

    assert_equal 1, seen.size
    assert_equal r2, seen[0].socket
  ensure
    [r1,w1,r2,w2].each { |io| io.close rescue nil }
  end
end
