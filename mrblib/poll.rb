class Poll
  def initialize
    @fds = {}
  end

  def add(socket, events = Poll::In)
    @fds[get_fd(socket)] = Fd.new(socket, events)
    self
  end

  def remove(socket)
    if @fds.delete(get_fd(socket))
      return self
    end
  end

  def update(socket, events)
    if fd = @fds[get_fd(socket)]
      fd.events = events
      return self
    end
  end

  def remove_update_or_add(socket, events)
    if events == 0
      @fds.delete(get_fd(socket))
    else
      if fd = @fds[get_fd(socket)]
        fd.events = events
      else
        @fds[get_fd(socket)] = Fd.new(socket, events)
      end
    end
    self
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
