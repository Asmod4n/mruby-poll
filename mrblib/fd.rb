class Poll
  class Fd
    attr_reader :socket
    attr_accessor :events, :revents

    def initialize(socket, events)
      @socket = socket
      @events = events
    end

    alias_method :fileno, :socket
    alias_method :to_i, :socket

    def readable?
      @revents & Poll::In != 0
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
