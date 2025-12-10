# RotorNet Simulator - Quick Start Guide

## New Features Summary

### 1. Virtual Output Queues (VOQs)
- **What**: Separate queue per destination rack at each ToR
- **Why**: Prevents head-of-line blocking
- **Configuration**: `queue_size_pkts` sets capacity per VOQ
- **Code**: See `voq.h` for implementation

### 2. Two-Hop Valiant Load Balancing
- **What**: Packets route through random intermediate rack
- **When**: Always for low-latency flows, adaptive for bulk
- **How**: Intermediate rack selected at flow creation
- **Code**: See `handleFlowArrival()` in `simulator.h`

### 3. Flow File I/O
- **Save flows**: Set `save_flows true` in config
- **Load flows**: Set `flow_file my_flows.csv` in config
- **Format**: CSV with rack/host separation
- **Benefit**: Reproducible experiments, cross-simulator comparisons

## Quick Examples

### Generate and Save Flows
```bash
# Create config_save.txt:
save_flows true
flow_output_file experiment_flows.csv
# ... other params ...

./rotornet_sim config_save.txt
# Creates experiment_flows.csv
```

### Reuse Saved Flows
```bash
# Create config_load.txt:
flow_file experiment_flows.csv
# ... other params ...

./rotornet_sim config_load.txt
# Uses pre-generated flows
```

### Convert Opera-sim Flows
```bash
# You have flows from opera-sim in opera_flows.txt
./flow_converter opera2rotor opera_flows.txt rotornet_flows.csv

# Use in RotorNet
echo "flow_file rotornet_flows.csv" >> config.txt
./rotornet_sim config.txt
```

### Convert to Opera-sim Format
```bash
# Generate flows with RotorNet
./rotornet_sim config_with_save.txt

# Convert for use in opera-sim
./flow_converter rotor2opera my_flows.csv for_opera.txt

# Use for_opera.txt in opera-sim
```

## Understanding the Output

### VOQ Statistics (future enhancement)
Currently, dropped packets indicate VOQ overflow. Future versions could track:
- Queue depth per destination
- HOL blocking incidents
- VOQ utilization

### VLB Statistics
- Low-latency flows: Always 2 hops (src → intermediate → dst)
- Bulk flows: Mostly 1 hop (direct), occasionally 2 hops if using VLB

### Flow Type Distribution
The simulator reports separate FCT statistics for:
- **Low-latency**: Flows < 15 MB (default threshold)
- **Bulk**: Flows ≥ 15 MB

## Architecture Notes

### Packet Flow (Low-latency with VLB)
```
1. Flow arrives → Select random intermediate rack
2. Create packets → Set intermediate_rack field
3. Enqueue at source → VOQ for intermediate dest
4. Transmit when path available
5. Arrive at intermediate → Set at_intermediate = true
6. Enqueue at intermediate → VOQ for final dest
7. Transmit to destination
8. Arrive at destination → Update flow stats
```

### Packet Flow (Bulk, direct path)
```
1. Flow arrives → intermediate_rack = -1 (direct)
2. Create packets
3. Enqueue at source → VOQ for final dest
4. Wait for direct path to destination
5. Transmit when circuit configured
6. Arrive at destination → Update flow stats
```

### VOQ Selection in startTransmission()
```cpp
1. Get all non-empty VOQs at this rack
2. Check which destinations have direct paths now
3. Select destination with direct path (if any)
4. If bulk traffic with no direct path:
   - Wait for direct path OR
   - Use VLB if queue pressure is high
5. Dequeue packet and transmit
```

## Common Issues

### "Queue full" / High packet drops
- **Cause**: VOQ capacity too small or load too high
- **Fix**: Increase `queue_size_pkts` or reduce `load_factor`

### Low throughput with bulk traffic
- **Cause**: Waiting for direct paths increases latency
- **Fix**: This is by design; bulk trades latency for bandwidth efficiency

### Flows not completing
- **Check**: Simulation time sufficient? (`sim_time_ms`)
- **Check**: Queue capacity adequate?
- **Debug**: Enable verbose logging (future feature)

## Performance Tips

### Fast Iteration
1. Generate flows once with `save_flows true`
2. Run multiple experiments loading from file
3. Vary network parameters without changing workload

### Reproducibility
- Always set `random_seed` in config
- Save flows to file
- Document config file with results

### Large-Scale Simulations
- Start small (16-32 racks) to validate
- Increase `num_racks` gradually
- Monitor memory usage (VOQs scale with num_racks²)
- Consider shorter `sim_time_ms` for initial tests

## Next Steps

1. Run default simulation: `make && ./rotornet_sim`
2. Save flows: Edit config, set `save_flows true`
3. Experiment with different workloads
4. Compare Opera-sim and RotorNet using converter
5. Analyze FCT distributions for bulk vs. low-latency

## Compatibility Checklist

### Opera-sim Workflow Compatibility
✅ Same workload distributions (VL2, DCTCP, Facebook)  
✅ Flow size/timing semantics preserved  
✅ Converter tool provided  
⚠️ Different internal format (CSV vs. space-separated)  
⚠️ Rack/host vs. global host numbering  
❌ Not binary-compatible (need converter)

### When to Use Each Format
- **RotorNet CSV**: Easier to parse, human-readable, rack-aware
- **Opera-sim**: Compatible with existing opera-sim experiments
- **Conversion**: Lossless in both directions (assuming 32 hosts/rack)
