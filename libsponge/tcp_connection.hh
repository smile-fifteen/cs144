#ifndef SPONGE_LIBSPONGE_TCP_FACTORED_HH
#define SPONGE_LIBSPONGE_TCP_FACTORED_HH
 
#include "tcp_config.hh"
#include "tcp_receiver.hh"
#include "tcp_sender.hh"
#include "tcp_state.hh"
#include <limits>
class TCPConnection {
  private:
    TCPConfig _cfg;
    TCPReceiver _receiver{_cfg.recv_capacity};
    TCPSender _sender{_cfg.send_capacity, _cfg.rt_timeout, _cfg.fixed_isn};
 
    std::queue<TCPSegment> _segments_out{};
 
    bool _linger_after_streams_finish{true};
    uint64_t _time_since_last_segment_received{0};
    bool _active{true};
  public:
 
    void connect();
 
    size_t write(const std::string &data);
 
    size_t remaining_outbound_capacity() const;
 
    void end_input_stream();
 
 
    ByteStream &inbound_stream() { return _receiver.stream_out(); }
 
 
    size_t bytes_in_flight() const;
    size_t unassembled_bytes() const;
    size_t time_since_last_segment_received() const;
    TCPState state() const { return {_sender, _receiver, active(), _linger_after_streams_finish}; };
 
 
    void segment_received(const TCPSegment &seg);
 
    void tick(const size_t ms_since_last_tick);
 
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
 
    bool active() const;
 
    explicit TCPConnection(const TCPConfig &cfg) : _cfg{cfg} {}
    void send_RST();
    void set_ackno_win(TCPSegment& seg);
    bool real_send();
    bool check_inbound_ended();
    bool check_outbound_ended();

    ~TCPConnection();  
    TCPConnection() = delete;
    TCPConnection(TCPConnection &&other) = default;
    TCPConnection &operator=(TCPConnection &&other) = default;
    TCPConnection(const TCPConnection &other) = delete;
    TCPConnection &operator=(const TCPConnection &other) = delete;
};
 
#endif  // SPONGE_LIBSPONGE_TCP_FACTORED_HH