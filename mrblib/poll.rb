class Poll
  class Fd < Struct.new(:fd, :events, :revents); end

  attr_reader :pollset

  def add(fd)
    @pollset << fd
    update
  end

  def remove(fd)
    @pollset.delete(fd)
    update
  end
end
