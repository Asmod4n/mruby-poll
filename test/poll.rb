assert("Poll basic add/remove and timeout") do
  poll = Poll.new
  fd = poll.add(STDOUT_FILENO, Poll::Out)
  assert_equal STDOUT_FILENO, fd
  poll.remove(fd) # no raise

  # Removing twice is a no-op
  poll.remove(fd)

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
  poll.add(STDOUT_FILENO, Poll::Out)
  ret = poll.update(STDOUT_FILENO, Poll::Out)
  assert_equal STDOUT_FILENO, ret
  assert_nil poll.update(STDERR_FILENO, Poll::In)
end

assert("Poll remove_update_or_add: remove, update, add") do
  poll = Poll.new
  poll.add(STDOUT_FILENO, Poll::Out)

  # remove path
  assert_nil poll.remove_update_or_add(STDOUT_FILENO, 0)
  assert_nil poll.update(STDOUT_FILENO, Poll::Out)

  # add path
  ret = poll.remove_update_or_add(STDOUT_FILENO, Poll::Out)
  assert_equal STDOUT_FILENO, ret

  # update path
  ret2 = poll.remove_update_or_add(STDOUT_FILENO, Poll::Out)
  assert_equal STDOUT_FILENO, ret2
end

assert("Poll clear returns self and empties set") do
  poll = Poll.new
  poll.add(STDOUT_FILENO, Poll::Out)
  poll.add(STDIN_FILENO,  Poll::In)
  assert_equal poll, poll.clear
  assert_nil poll.update(STDOUT_FILENO, Poll::Out)
  assert_nil poll.update(STDIN_FILENO,  Poll::In)
end

assert("Remove inside wait using a pipe") do
    r, w = IO.pipe
    begin
        poll = Poll.new
        pollfd = poll.add(r.fileno, Poll::In)

        # Make r readable
        w.write("x")

        called = false
        poll.wait(100) do |fd, revents|
        called = true
        assert_equal r.fileno, fd
        assert_equal Poll::In, revents
        poll.remove(pollfd) # safe inside callback
        end
        assert_true called

        # After wait/compact, r is gone
        assert_nil poll.update(r.fileno, Poll::In)
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
        poll.add(r1.fileno, Poll::In)
        poll.add(r2.fileno, Poll::In)

        # Make only r2 readable
        w2.write("y")

        seen = []
        poll.wait(100) { |fd, revents| seen << [fd, revents] }

        assert_equal 1, seen.size
        assert_equal r2.fileno, seen[0][0]
        assert_equal Poll::In,  seen[0][1]
    ensure
        [r1,w1,r2,w2].each { |io| io.close rescue nil }
    end
end
