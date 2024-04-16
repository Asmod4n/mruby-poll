class Poll
  attr_reader :fds

  def remove_update_or_add(socket, pollmask)
    fds.each do |pfd|
      if pfd.socket == socket
        if pollmask == 0
          remove(pfd)
          return nil
        else
          pfd.events = pollmask
          return pfd
        end
      end
    end
    add(socket, pollmask)
  end
end
