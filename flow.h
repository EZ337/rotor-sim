// flow.h - Flow and packet data structures
#ifndef FLOW_H
#define FLOW_H

#include <cstdint>
#include <vector>

enum class FlowType {
    BULK,
    LOW_LATENCY
};

struct Packet {
    uint64_t id;
    uint64_t flow_id;
    int src_rack;
    int dst_rack;
    int src_host;
    int dst_host;
    int size_bytes;
    double creation_time;
    double sent_time;
    double arrival_time;
    FlowType type;
    bool dropped;
    int hop_count;
};

struct Flow {
    uint64_t id;
    int src_rack;
    int dst_rack;
    int src_host;
    int dst_host;
    uint64_t size_bytes;
    double start_time;
    double completion_time;
    FlowType type;
    
    std::vector<uint64_t> packet_ids;
    int packets_sent;
    int packets_received;
    bool completed;
    
    Flow() : id(0), src_rack(0), dst_rack(0), src_host(0), dst_host(0),
             size_bytes(0), start_time(0), completion_time(0),
             type(FlowType::BULK), packets_sent(0), packets_received(0),
             completed(false) {}
    
    double getFCT() const {
        if (!completed) return -1.0;
        return completion_time - start_time;
    }
    
    int getNumPackets(int mtu) const {
        return (size_bytes + mtu - 1) / mtu;
    }
};

#endif // FLOW_H
