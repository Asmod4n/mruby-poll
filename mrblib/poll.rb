class Poll
  def initialize
    @fds = {}
  end

  def add(socket, events = Poll::In)
    @fds[get_fd(socket)] = fd = Fd.new(socket, events)
    fd
  end

  def remove(socket)
    @fds.delete(get_fd(socket))
  end

  def update(socket, events)
    if fd = @fds[get_fd(socket)]
      fd.events = events
    end
    fd
  end

  def remove_update_or_add(socket, events)
    if events == 0
      remove(socket)
    else
      if fd = @fds[get_fd(socket)]
        fd.events = events
        fd
      else
        @fds[get_fd(socket)] = fd = Fd.new(socket, events)
        fd
      end
    end
  end

  def clear
    @fds.clear
    self
  end

  private
  def get_fd(socket)
    case socket
    when Integer
      socket
    when IO
      socket.fileno
    else
      socket.to_i
    end
  end
end
