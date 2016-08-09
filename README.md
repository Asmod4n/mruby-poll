# mruby-poll
Low level system poll for mruby

Example
=======

```ruby
poll = Poll.new

pollfd = poll.add(STDOUT_FILENO, Poll::Out)

if ready_fds = poll.wait
  ready_fds.each do |ready_fd|
    puts ready_fd
  end
end
```

"Documentation"
=============

```ruby
class Poll
  def add(socket, events = Poll::In) # adds a Ruby Socket/IO Object to the pollfds
  # returns a Poll::_Fd
  def remove(fd) # deletes a Poll::_Fd struct from the pollfds
  def wait((optional) (int) timeout) # waits until a fd becomes ready, its using the poll function from <poll.h>
  # returns "false" when the process gets interrupted
  # returns "nil" when it times out
  # returns a array when ready fds have been found
  # raises exceptions according to other errors
end
```

For other errors take a look at http://pubs.opengroup.org/onlinepubs/007908799/xsh/poll.html
