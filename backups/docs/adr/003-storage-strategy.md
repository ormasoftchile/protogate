# ADR-003: Storage Strategy

**Status**: Accepted  
**Date**: 2025-01-23  
**Deciders**: Architecture Team  
**Context Owner**: Core Server Development

---

## Context

Protogate Core Server needs to store and retrieve tunnel metadata for routing decisions. Key requirements:

1. **Low latency**: <1ms lookup time for routing decisions
2. **High availability**: Single point of failure unacceptable
3. **Consistency**: Strong consistency for tunnel registration/deletion
4. **Scalability**: Support 1000+ tunnels per region
5. **Durability**: Tunnel metadata must survive server restarts
6. **Cost**: Minimize storage costs (<$5/month for 100 tunnels)

**Problem Statement**: How should we store tunnel metadata for fast routing and high availability?

---

## Decision

**We will use in-memory storage (std::unordered_map) with optional Azure Blob Storage backup.**

---

## Storage Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Container App Instance                                      │
│                                                             │
│  ┌───────────────────────────────────────────────────────┐ │
│  │ In-Memory Cache (Primary)                             │ │
│  │ - std::unordered_map<tunnel_id, Tunnel>               │ │
│  │ - std::shared_mutex for read/write locking            │ │
│  │ - O(1) lookup, <1ms latency                           │ │
│  └───────────────────┬───────────────────────────────────┘ │
│                      │                                      │
│                      │ Async backup (optional)              │
│                      ▼                                      │
│  ┌───────────────────────────────────────────────────────┐ │
│  │ Azure Blob Storage (Backup)                           │ │
│  │ - JSON files: tunnels/{tunnel_id}.json                │ │
│  │ - Written on tunnel create/update/delete              │ │
│  │ - Read on server startup (cache warm-up)              │ │
│  └───────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## Options Considered

### Option 1: In-Memory + Blob Backup ✅ **SELECTED**

**Architecture**:
- Primary: `std::unordered_map` with `std::shared_mutex` (read-write lock)
- Backup: Azure Blob Storage (JSON files, one per tunnel)
- Consistency: Write-through (sync) or write-behind (async)

**Pros**:
- ✅ **Ultra-low latency**: <1ms lookup (in-memory hash table)
- ✅ **Simple implementation**: No external DB client, minimal dependencies
- ✅ **Cost-effective**: Blob storage costs ~$0.02/GB/month (negligible for metadata)
- ✅ **Customer-controlled**: Blob storage in customer's Azure subscription
- ✅ **Horizontal scaling**: Each replica has independent cache, no DB bottleneck
- ✅ **Disaster recovery**: Blob backup enables restore after crash

**Cons**:
- ⚠️ **Cache inconsistency**: Multiple replicas have independent caches (eventual consistency)
- ⚠️ **Cold start penalty**: Must read from Blob on startup (~100ms for 100 tunnels)
- ⚠️ **No distributed locking**: Race conditions possible during concurrent updates
- ⚠️ **Memory usage**: All tunnels in memory (~1KB per tunnel metadata)

**Consistency Model**:
- **Single replica**: Strong consistency (all requests hit same cache)
- **Multiple replicas**: Eventual consistency (updates propagate via Blob + gossip)
- **Mitigation**: Management API routes all writes to single "leader" replica (Azure Container Apps session affinity)

**Performance**:
- Lookup: O(1), <1ms
- Insert: O(1), <1ms + async Blob write (100ms)
- Memory: ~1KB per tunnel (100 tunnels = 100KB)

**Code Example**:
```cpp
class TunnelCache {
    std::unordered_map<std::string, Tunnel> tunnels_;
    mutable std::shared_mutex mutex_;
    BlobStorageClient blob_client_;
    
public:
    std::optional<Tunnel> get(const std::string& tunnel_id) const {
        std::shared_lock lock(mutex_);
        auto it = tunnels_.find(tunnel_id);
        return (it != tunnels_.end()) ? std::optional(it->second) : std::nullopt;
    }
    
    void insert(const Tunnel& tunnel) {
        {
            std::unique_lock lock(mutex_);
            tunnels_[tunnel.id] = tunnel;
        }
        // Async write to Blob Storage
        blob_client_.write_async("tunnels/" + tunnel.id + ".json", tunnel.to_json());
    }
};
```

---

### Option 2: Azure Table Storage

**Pros**:
- ✅ Distributed storage (no single point of failure)
- ✅ Automatic replication (3 copies)
- ✅ Low cost (~$0.05/GB/month)
- ✅ Strong consistency (within partition)

**Cons**:
- ❌ **High latency**: 10-50ms per query (unacceptable for routing hot path)
- ❌ **Throughput limits**: 2000 ops/sec per partition
- ❌ **Dependency**: Requires Azure SDK, adds complexity
- ❌ **Cold start**: 100ms+ for query latency on first request

**Verdict**: Rejected due to high latency (10-50ms vs <1ms in-memory).

---

### Option 3: Azure Cosmos DB

**Pros**:
- ✅ Global distribution
- ✅ Multiple consistency models (eventual to strong)
- ✅ Auto-indexing
- ✅ Low latency (5-10ms)

**Cons**:
- ❌ **Cost**: $25-100+/month (400 RU/s minimum), exceeds budget
- ❌ **Overkill**: Features like multi-region replication unnecessary for tunnel metadata
- ❌ **Latency**: 5-10ms still too high for routing hot path
- ❌ **Complexity**: Requires partition key design, SDK integration

**Verdict**: Rejected due to cost ($25+/month vs <$1/month for Blob) and latency.

---

### Option 4: Redis (Azure Cache for Redis)

**Pros**:
- ✅ In-memory performance (<1ms)
- ✅ Distributed caching (shared state across replicas)
- ✅ Pub/Sub for cache invalidation
- ✅ Strong consistency with transactions

**Cons**:
- ❌ **Cost**: $15+/month for Basic tier (250MB)
- ❌ **Dependency**: Requires Redis client (hiredis or cpp_redis)
- ❌ **Complexity**: Connection pooling, failover handling
- ❌ **Over-engineering**: Full distributed cache for <1MB of data

**Verdict**: Rejected due to cost and complexity. Redis is overkill for this use case.

---

### Option 5: SQLite (Embedded Database)

**Pros**:
- ✅ Embedded (no external service)
- ✅ ACID transactions
- ✅ SQL queries for management API
- ✅ Low latency (1-10ms)

**Cons**:
- ❌ **File I/O overhead**: Slower than in-memory (1-10ms vs <1ms)
- ❌ **Locking**: Write lock blocks all reads (WAL mode mitigates, but adds complexity)
- ❌ **Horizontal scaling**: Each replica has separate SQLite file (same consistency issues as in-memory)
- ❌ **Container Apps ephemeral storage**: SQLite file lost on restart unless backed up

**Verdict**: Rejected. No advantage over in-memory + Blob backup, and slower.

---

### Option 6: Shared Filesystem (Azure Files)

**Pros**:
- ✅ Shared state across replicas
- ✅ POSIX filesystem API
- ✅ Automatic durability

**Cons**:
- ❌ **Latency**: 10-20ms for file operations (network filesystem)
- ❌ **Locking complexity**: Distributed file locking is error-prone
- ❌ **Cost**: $0.06/GB/month + transaction costs
- ❌ **POSIX limitations**: Azure Files not full POSIX (e.g., no fcntl locking)

**Verdict**: Rejected due to high latency and locking complexity.

---

## Rationale

**In-memory + Blob backup was selected because**:

1. **Performance**: <1ms lookup latency, critical for routing hot path (every HTTP request)

2. **Simplicity**: No external DB client, minimal dependencies, easier to debug

3. **Cost**: Blob storage costs <$1/month for 1000 tunnels (vs $15+ for Redis, $25+ for Cosmos)

4. **Consistency model**: Acceptable for this use case:
   - Tunnels are created infrequently (minutes/hours apart)
   - Eventual consistency across replicas is acceptable (routing still works)
   - Management API uses session affinity to route all writes to single replica

5. **Customer control**: Blob storage in customer's Azure subscription, no external SaaS

6. **Disaster recovery**: Blob backup enables restore after crash or scale-out

**Trade-offs accepted**:
- Eventual consistency across replicas (mitigated by session affinity for writes)
- Cold start penalty (100ms to load from Blob on startup)
- Manual cache invalidation (no automatic Pub/Sub)

---

## Consequences

### Positive

- ✅ **Ultra-low latency**: <1ms routing decisions, meets <10ms p95 HTTP latency goal
- ✅ **Simple codebase**: No complex DB client, easier to maintain
- ✅ **Cost-effective**: <$1/month storage cost vs $15+ for managed cache
- ✅ **Horizontal scaling**: Each replica is independent, no DB bottleneck
- ✅ **Customer-controlled**: Blob storage in customer's Azure subscription

### Negative

- ⚠️ **Eventual consistency**: Tunnels created on one replica may not be visible on others for 1-2 seconds
- ⚠️ **Cold start**: Server startup takes 100ms longer to load from Blob
- ⚠️ **Memory usage**: All tunnels in memory (~100KB for 100 tunnels, acceptable)

### Neutral

- 🔄 **Management API routing**: Session affinity required for write operations (easily configured in Container Apps)
- 🔄 **Cache invalidation**: Manual invalidation via Management API (acceptable for low-frequency updates)

---

## Implementation Notes

**Cache class**:
```cpp
class TunnelCache {
private:
    std::unordered_map<std::string, models::Tunnel> tunnels_;
    mutable std::shared_mutex mutex_;
    std::unique_ptr<BlobStorageClient> blob_client_;

public:
    // O(1) read with shared lock
    std::optional<models::Tunnel> get(const std::string& tunnel_id) const {
        std::shared_lock lock(mutex_);
        auto it = tunnels_.find(tunnel_id);
        return (it != tunnels_.end()) ? std::optional(it->second) : std::nullopt;
    }
    
    // O(1) write with unique lock + async Blob backup
    void insert(const models::Tunnel& tunnel) {
        {
            std::unique_lock lock(mutex_);
            tunnels_[tunnel.id] = tunnel;
        }
        
        // Async Blob write (fire-and-forget)
        blob_client_->write_async(
            "tunnels/" + tunnel.id + ".json",
            tunnel.to_json(),
            [](bool success) {
                if (!success) {
                    Logger::warning("Blob backup failed");
                }
            }
        );
    }
    
    // Load from Blob on startup
    void load_from_blob() {
        auto blobs = blob_client_->list("tunnels/");
        for (const auto& blob_name : blobs) {
            auto json = blob_client_->read(blob_name);
            auto tunnel = models::Tunnel::from_json(json);
            tunnels_[tunnel.id] = tunnel;
        }
        Logger::info("Loaded " + std::to_string(tunnels_.size()) + " tunnels from Blob");
    }
};
```

**Blob Storage structure**:
```
container: protogate-tunnels
├── tunnels/
│   ├── my-tunnel.json
│   ├── printer1.json
│   └── api-dev.json
└── metadata/
    └── last-sync.json
```

**Consistency guarantees**:
- **Single replica**: Strong consistency (all operations on same in-memory cache)
- **Multiple replicas**: Eventual consistency (Blob acts as source of truth)
- **Write path**: Management API → Single replica (session affinity) → In-memory + Blob
- **Read path**: Any replica → In-memory (may be stale for 1-2 seconds)

**Failure scenarios**:
| Scenario | Impact | Recovery |
|----------|--------|----------|
| Replica crashes | Tunnels lost from that replica's cache | Load from Blob on restart (100ms) |
| Blob write fails | Tunnel in memory but not persisted | Retry on next write or manual sync |
| All replicas crash | All tunnels lost from memory | Load from Blob on any replica restart |
| Blob storage unavailable | Cannot persist new tunnels | Queue writes in memory, flush when Blob available |

---

## Monitoring

**Metrics**:
- Cache hit rate (should be 100% after warm-up)
- Cache size (number of tunnels in memory)
- Blob write latency (p50, p95, p99)
- Blob read errors (failed backups)

**Alerts**:
- Cache miss rate >1% (indicates Blob sync issue)
- Blob write failure rate >5%
- Cold start duration >500ms (slow Blob load)

---

## Future Enhancements

**Phase 1 (Current)**: In-memory + Blob backup
- ✅ Simplest implementation
- ✅ Meets latency requirements
- ⚠️ Eventual consistency across replicas

**Phase 2 (Future)**: Add cache invalidation via Azure Service Bus
- Pub/Sub for cache invalidation messages
- Eventual consistency <100ms (vs 1-2 seconds currently)
- Cost: +$1/month for Service Bus Basic tier

**Phase 3 (Future)**: Evaluate Azure Cache for Redis
- If cost becomes acceptable ($15/month)
- Strong consistency across replicas
- Native distributed locking

---

## References

- [Azure Blob Storage Pricing](https://azure.microsoft.com/en-us/pricing/details/storage/blobs/)
- [Azure Table Storage vs Cosmos DB](https://docs.microsoft.com/en-us/azure/cosmos-db/table-storage-overview)
- [C++ std::shared_mutex](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [Azure SDK for C++ (Blob Storage)](https://github.com/Azure/azure-sdk-for-cpp/tree/main/sdk/storage/azure-storage-blobs)

---

**Status**: Accepted and implemented  
**Last Reviewed**: 2025-01-23  
**Next Review**: After T109 (performance benchmarks) or if consistency issues arise
