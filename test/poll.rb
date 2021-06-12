assert("Poll") do
    poll = Poll.new

    pollfd = poll.add(STDOUT_FILENO, Poll::Out)
    ready_fds = poll.wait
    assert_equal(STDOUT_FILENO, ready_fds.first.socket)
    assert_true(ready_fds.first.writable?)
end
  