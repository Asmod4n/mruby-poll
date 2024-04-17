# mruby-poll
Low level system poll for mruby

Example without a block
=======================

```ruby
poll = Poll.new

pollfd = poll.add(STDOUT_FILENO, Poll::Out)

if ready_fds = poll.wait
  ready_fds.each do |ready_fd|
    puts ready_fd
  end
end
```

Example with a block
====================
```ruby
poll = Poll.new

pollfd = poll.add(STDOUT_FILENO, Poll::Out)

poll.wait do |ready_fd|
  if ready_fd == pollfd
    puts "got STDOUT_FILENO"
  end
end
```

"Documentation"
=============

```ruby
class Poll
  def add(socket, events = Poll::In) # adds a Ruby Socket/IO Object to the pollfds
  # returns a Poll::Fd
  def remove(fd) # deletes a Poll::Fd struct from the pollfds
  def wait((optional) (int) timeout, &block) # waits until a fd becomes ready, its using the poll function from <sys/poll.h>
  # returns "false" when the process gets interrupted
  # returns "nil" when it times out
  # returns a array when ready fds have been found, or self when a block is passed
  # raises exceptions according to other errors
end
```

For other errors take a look at http://pubs.opengroup.org/onlinepubs/007908799/xsh/poll.html


```ruby
class Poll
  class Fd
    def socket() # returns the socket
    def socket=(socket) # # sets another socket
    def events() # returns the events
    def events=(events) # sets events
    def revents() # returns the events after a poll()
    def revents=(revents) # sets revents
    def readable? # returns if the socket is readable
    def writable? # returns if the socket is writable
    def err? # returns if the socket is in a error state
    def disconnected? # returns if the socket is disconnected
  end
end
```

Events

```ruby
Poll::Err
Poll::Hub
Poll::In
Poll::NVal
Poll::Out
Poll::Pri
```

For what these events mean take a look at http://pubs.opengroup.org/onlinepubs/007908799/xsh/poll.html
