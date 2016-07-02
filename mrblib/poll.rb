class Poll
  class Fd < Struct.new(:fd, :events, :revents) do
      def readable?
        revents & In != 0
      end

      def writable?
        revents & Out != 0
      end

      def error?
        revents & Err != 0
      end
    end
  end

  attr_reader :fds

  def add(fd)
    @fds << fd
    update
  end

  alias :<< :add

  def remove(fd)
    @fds.delete(fd)
    update
  end
end
