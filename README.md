# mruby-poll
Low level system poll for mruby

Example
=======

```ruby
poll = Poll.new

fd = Poll::Fd.new
fd.fd = STDOUT_FILENO
fd.events = Poll::Out

poll << fd

if poll.wait
  poll.fds.each do |pollfd|
    puts pollfd.writable?
  end
end
```

Documentation
=============

```ruby
class Poll
  def add(fd) # adds a Poll::Fd struct to the pollfds, its equivalent to the pollfd struct from <poll.h>
  alias :<< :add
  def remove(fd) # deletes a Poll::Fd struct from the pollfds
  def wait((optional) (int) timeout) # waits until a fd becomes ready, its equivalent to the poll function from <poll.h>
  # returns "false" when the process gets interrupted
  # returns "nil" when wait times out
  # raises exceptions according to other errors
  private

  def __update # allocates a pollfd struct large enough to hold all pollfds
end
```

For other errors take a look at http://pubs.opengroup.org/onlinepubs/007908799/xsh/poll.html
