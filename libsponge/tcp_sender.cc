#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>
// #include <iostream>
#include <algorithm>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _rto{retx_timeout} {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    uint16_t window_size = _receiver_window_size ? _receiver_window_size : 1;
    while(window_size > _bytes_in_flight){
        TCPSegment seg;
        //设置syn
        if(!_syn_sent){
            seg.header().syn = true;
            _syn_sent = true;
        }
        //设置序列号
        seg.header().seqno = wrap(_next_seqno, _isn);
        //设置可以发送的最大长度
        const size_t payload_size = min({_stream.buffer_size(),
                                         static_cast<size_t>(window_size-_bytes_in_flight-seg.header().syn),
                                         static_cast<size_t>(TCPConfig::MAX_PAYLOAD_SIZE)});
        string payload = _stream.read(payload_size);
        //设置fin标志位，只有窗口足够且没发过fin才会设置
        if(!_fin_sent && _stream.eof() && payload_size+_bytes_in_flight+1 <= window_size){
            _fin_sent = true;
            seg.header().fin = true;
        }
        //装载数据
        seg.payload() = Buffer(move(payload));
        //没有数据什么都不做
        if(seg.length_in_sequence_space() == 0) break;
        //发送数据,启动计时器
        _segments_out.push(seg);
        if(!_timer_running){
            _timer_running = true;
            _time_elapsed = 0;
        }
        //增加在发送中的数据和序列号,追踪已发送的包
        _bytes_in_flight += seg.length_in_sequence_space();
        _next_seqno += seg.length_in_sequence_space();
        _segments_outstanding.push(seg);
        //没有数据可以传送
        if(seg.header().fin){
            break;
        }

    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t absno = unwrap(ackno, _isn, _next_seqno);
    //如果absno不合法则放弃
    if(!_ack_valid(absno)) return;
    //设置新的窗口大小
    _receiver_window_size = window_size;
    while(!_segments_outstanding.empty()){
        const TCPSegment& seg = _segments_outstanding.front();
        //当前seg已经全部被接受
        if(unwrap(seg.header().seqno, _isn, _next_seqno) + seg.length_in_sequence_space() <= absno){
            //去掉在发送的字节，移除seg
            _bytes_in_flight -= seg.length_in_sequence_space();
            _segments_outstanding.pop();
            //设置计时器
            _time_elapsed = 0;
            _rto = _initial_retransmission_timeout;
            _consecutive_retransmissions = 0;
        }else{
            break;
        }
        //如果没有数据则停止计时器
        if(!_bytes_in_flight){
            _timer_running = false;
        }
        fill_window();
    }

}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if(!_timer_running) return;

    _time_elapsed += ms_since_last_tick;
    if(_time_elapsed >= _rto){
        _time_elapsed = 0;
        _segments_out.push(_segments_outstanding.front());
        if(_receiver_window_size || _segments_outstanding.front().header().syn){
            _rto <<= 1;
            _consecutive_retransmissions += 1;
        }
    }
    // if (!_timer_running)
    //     return;
    // _time_elapsed += ms_since_last_tick;
    // // cout << "time_elapsed " << _time_elapsed << " rto " << _rto << " conti " << _consecutive_retransmissions << "\n";
    // if (_time_elapsed >= _rto) {
    //     _segments_out.push(_segments_outstanding.front());
    //     if (_receiver_window_size || _segments_outstanding.front().header().syn) {
    //         ++_consecutive_retransmissions;
    //         _rto <<= 1;
    //     }
    //     _time_elapsed = 0;
    // }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}

//ack的合法规则：
//  1.小于等于未发送的序列号
//  2.大于等于未ack的最早发送的序列号
bool TCPSender::_ack_valid(uint64_t abs_ackno) {
    if (_segments_outstanding.empty())
        return abs_ackno <= _next_seqno;
    return abs_ackno <= _next_seqno &&
           abs_ackno >= unwrap(_segments_outstanding.front().header().seqno, _isn, _next_seqno);
}