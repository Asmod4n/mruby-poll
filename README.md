# mruby-poll
Low level system poll for mruby

Example
=======

```ruby
poll = Poll.new

fd = Poll::Fd.new
fd.fd = STDOUT_FILENO
fd.events = Poll::Out

poll.add(fd)

if poll.wait
  poll.pollset.each do |pollfd|
    puts pollfd.revents
  end
end
```
