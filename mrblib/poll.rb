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
