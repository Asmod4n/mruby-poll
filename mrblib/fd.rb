class Poll
  class Fd
    attr_reader   :socket
    attr_accessor :events, :revents

    def initialize(socket, events)
      @socket, @events = socket, events
    end

    def readable?
      @revents & Poll::In  != 0
    end

    def writable?
      @revents & Poll::Out != 0
    end

    def error?
      @revents & Poll::Err != 0
    end

    def disconnected?
      @revents & Poll::Hup != 0
    end
  end
end
