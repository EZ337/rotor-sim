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
    int src_host;
    int dst_host;
    int size_bytes;
    double creation_time;
    double sent_time;
    double arrival_time;
    FlowType type;
    bool dropped;
    bool at_intermediate;
    
    // Routing metadata for 1-hop and 2-hop paths
    // INVARIANT: final_dst never changes after packet creation
    int final_dst;      // Ultimate destination rack (never changes)
    
    // INVARIANT: current_dst is the next-hop target for this packet
    // - On first hop: current_dst = intermediate rack (for VLB) or final_dst (for direct)
    // - On second hop: current_dst = final_dst
    int current_dst;    // Next hop destination for this packet
    
    // INVARIANT: hop_count tracks routing progress
    // - 0: Just created, not yet transmitted
    // - 1: First hop complete (at intermediate or final)
    // - 2: Second hop complete (only for 2-hop paths)
    int hop_count;      // 0=new, 1=after first hop, 2=delivered
};

struct Flow {
    uint64_t id;
    int src_rack;
    int dst_rack;  // This is the final destination
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
    
    // Flow completion time accounts for all hops (1 or 2)
    double getFCT() const {
        if (!completed) return -1.0;
        return completion_time - start_time;
    }
    
    int getNumPackets(int mtu) const {
        return (size_bytes + mtu - 1) / mtu;
    }
};

#endif // FLOW_H
