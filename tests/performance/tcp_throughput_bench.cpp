// tests/performance/tcp_throughput_bench.cpp
// TCP throughput performance benchmarks

#include <benchmark/benchmark.h>
#include "../../src/proxy/tcp_proxy.h"
#include "../../src/proxy/protocol_multiplexer.h"
#include "../../src/agent/agent_registry.h"
#include "../../src/storage/cache.h"
#include "../../src/models/tunnel.h"
#include "../../src/core/io_context_pool.h"
#include <boost/asio.hpp>
#include <thread>
#include <vector>
#include <random>

using namespace protogate;
namespace asio = boost::asio;

// Global test fixtures
static std::shared_ptr<core::IOContextPool> g_io_pool;
static std::shared_ptr<agent::AgentRegistry> g_agent_registry;
static std::shared_ptr<storage::Cache<std::string, models::Tunnel>> g_tunnel_cache;
static std::unique_ptr<proxy::TCPProxy> g_tcp_proxy;
static std::thread g_io_thread;

// Initialize test infrastructure once
static void SetupBenchmark() {
    if (!g_io_pool) {
        g_io_pool = std::make_shared<core::IOContextPool>(4); // 4 threads for performance
        g_agent_registry = std::make_shared<agent::AgentRegistry>(100);
        g_tunnel_cache = std::make_shared<storage::Cache<std::string, models::Tunnel>>(1000);
        
        g_tcp_proxy = std::make_unique<proxy::TCPProxy>(
            g_io_pool,
            g_agent_registry,
            g_tunnel_cache
        );
        
        // Configure tunnel
        models::Tunnel tunnel;
        tunnel.tunnel_id = "bench_printer";
        tunnel.target_host = "localhost";
        tunnel.target_port = 9100;
        tunnel.protocol = models::TunnelProtocol::TCP;
        tunnel.status = models::TunnelStatus::ACTIVE;
        g_tunnel_cache->set("bench_printer", tunnel, std::chrono::hours(1));
        
        // Start IO thread pool
        g_io_thread = std::thread([]() {
            g_io_pool->run();
        });
    }
}

static void TeardownBenchmark() {
    if (g_io_pool) {
        g_io_pool->stop();
        if (g_io_thread.joinable()) {
            g_io_thread.join();
        }
        g_tcp_proxy.reset();
        g_tunnel_cache.reset();
        g_agent_registry.reset();
        g_io_pool.reset();
    }
}

// Benchmark: Small packet throughput (printer control commands - 256 bytes)
static void BM_SmallPacketThroughput(benchmark::State& state) {
    SetupBenchmark();
    
    auto& io_context = g_io_pool->get_io_context();
    asio::ip::tcp::socket socket(io_context);
    asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    auto local_port = acceptor.local_endpoint().port();
    
    boost::system::error_code ec;
    socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), local_port), ec);
    if (ec) {
        state.SkipWithError("Connection failed");
        return;
    }
    
    std::vector<uint8_t> data(256); // 256-byte control commands
    std::iota(data.begin(), data.end(), 0);
    
    size_t total_bytes = 0;
    
    for (auto _ : state) {
        size_t bytes_sent = asio::write(socket, asio::buffer(data), ec);
        if (ec) {
            state.SkipWithError("Write failed");
            break;
        }
        total_bytes += bytes_sent;
    }
    
    socket.close();
    
    state.SetBytesProcessed(total_bytes);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SmallPacketThroughput)->Unit(benchmark::kMicrosecond);

// Benchmark: Medium packet throughput (typical printer data - 4KB)
static void BM_MediumPacketThroughput(benchmark::State& state) {
    SetupBenchmark();
    
    auto& io_context = g_io_pool->get_io_context();
    asio::ip::tcp::socket socket(io_context);
    asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    auto local_port = acceptor.local_endpoint().port();
    
    boost::system::error_code ec;
    socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), local_port), ec);
    if (ec) {
        state.SkipWithError("Connection failed");
        return;
    }
    
    std::vector<uint8_t> data(4 * 1024); // 4KB printer data chunks
    std::iota(data.begin(), data.end(), 0);
    
    size_t total_bytes = 0;
    
    for (auto _ : state) {
        size_t bytes_sent = asio::write(socket, asio::buffer(data), ec);
        if (ec) {
            state.SkipWithError("Write failed");
            break;
        }
        total_bytes += bytes_sent;
    }
    
    socket.close();
    
    state.SetBytesProcessed(total_bytes);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MediumPacketThroughput)->Unit(benchmark::kMicrosecond);

// Benchmark: Large packet throughput (full printer pages - 64KB)
static void BM_LargePacketThroughput(benchmark::State& state) {
    SetupBenchmark();
    
    auto& io_context = g_io_pool->get_io_context();
    asio::ip::tcp::socket socket(io_context);
    asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    auto local_port = acceptor.local_endpoint().port();
    
    boost::system::error_code ec;
    socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), local_port), ec);
    if (ec) {
        state.SkipWithError("Connection failed");
        return;
    }
    
    std::vector<uint8_t> data(64 * 1024); // 64KB window size
    std::iota(data.begin(), data.end(), 0);
    
    size_t total_bytes = 0;
    
    for (auto _ : state) {
        size_t bytes_sent = asio::write(socket, asio::buffer(data), ec);
        if (ec) {
            state.SkipWithError("Write failed");
            break;
        }
        total_bytes += bytes_sent;
    }
    
    socket.close();
    
    state.SetBytesProcessed(total_bytes);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LargePacketThroughput)->Unit(benchmark::kMicrosecond);

// Benchmark: Streaming throughput - sustained 1MB transfers
static void BM_StreamingThroughput(benchmark::State& state) {
    SetupBenchmark();
    
    auto& io_context = g_io_pool->get_io_context();
    asio::ip::tcp::socket socket(io_context);
    asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    auto local_port = acceptor.local_endpoint().port();
    
    boost::system::error_code ec;
    socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), local_port), ec);
    if (ec) {
        state.SkipWithError("Connection failed");
        return;
    }
    
    std::vector<uint8_t> data(1024 * 1024); // 1MB continuous stream
    std::iota(data.begin(), data.end(), 0);
    
    size_t total_bytes = 0;
    
    for (auto _ : state) {
        size_t bytes_sent = asio::write(socket, asio::buffer(data), ec);
        if (ec) {
            state.SkipWithError("Write failed");
            break;
        }
        total_bytes += bytes_sent;
    }
    
    socket.close();
    
    state.SetBytesProcessed(total_bytes);
    state.SetItemsProcessed(state.iterations());
    
    // Calculate and report Mbps
    double seconds = state.iterations() * state.counters["time_per_iteration"] / 1e9;
    double mbps = (total_bytes * 8.0) / (1024.0 * 1024.0 * seconds);
    state.counters["Mbps"] = mbps;
}
BENCHMARK(BM_StreamingThroughput)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(10);

// Benchmark: Concurrent connections throughput (4 parallel clients)
static void BM_ConcurrentConnectionThroughput(benchmark::State& state) {
    SetupBenchmark();
    
    const int num_clients = 4;
    std::vector<std::thread> client_threads;
    std::atomic<size_t> total_bytes{0};
    std::atomic<bool> running{true};
    
    // Start concurrent clients
    for (int i = 0; i < num_clients; ++i) {
        client_threads.emplace_back([&, i]() {
            auto& io_context = g_io_pool->get_io_context();
            asio::ip::tcp::socket socket(io_context);
            asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
            auto local_port = acceptor.local_endpoint().port();
            
            boost::system::error_code ec;
            socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), local_port), ec);
            if (ec) {
                return;
            }
            
            std::vector<uint8_t> data(16 * 1024); // 16KB per client
            std::fill(data.begin(), data.end(), static_cast<uint8_t>(i));
            
            while (running) {
                size_t bytes_sent = asio::write(socket, asio::buffer(data), ec);
                if (ec) {
                    break;
                }
                total_bytes += bytes_sent;
            }
            
            socket.close();
        });
    }
    
    // Benchmark measurement
    for (auto _ : state) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Stop clients
    running = false;
    for (auto& thread : client_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    state.SetBytesProcessed(total_bytes.load());
    state.SetItemsProcessed(state.iterations() * num_clients);
}
BENCHMARK(BM_ConcurrentConnectionThroughput)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(20);

// Benchmark: Random-size packet throughput (realistic mixed traffic)
static void BM_MixedPacketSizes(benchmark::State& state) {
    SetupBenchmark();
    
    auto& io_context = g_io_pool->get_io_context();
    asio::ip::tcp::socket socket(io_context);
    asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    auto local_port = acceptor.local_endpoint().port();
    
    boost::system::error_code ec;
    socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), local_port), ec);
    if (ec) {
        state.SkipWithError("Connection failed");
        return;
    }
    
    // Random number generator for packet sizes
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> size_dist(256, 65536); // 256B - 64KB
    
    size_t total_bytes = 0;
    
    for (auto _ : state) {
        size_t packet_size = size_dist(rng);
        std::vector<uint8_t> data(packet_size);
        std::iota(data.begin(), data.end(), 0);
        
        size_t bytes_sent = asio::write(socket, asio::buffer(data), ec);
        if (ec) {
            state.SkipWithError("Write failed");
            break;
        }
        total_bytes += bytes_sent;
    }
    
    socket.close();
    
    state.SetBytesProcessed(total_bytes);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MixedPacketSizes)->Unit(benchmark::kMicrosecond);

// Benchmark: Zero-copy forwarding efficiency
static void BM_ZeroCopyForwarding(benchmark::State& state) {
    SetupBenchmark();
    
    auto& io_context = g_io_pool->get_io_context();
    asio::ip::tcp::socket socket(io_context);
    asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    auto local_port = acceptor.local_endpoint().port();
    
    boost::system::error_code ec;
    socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), local_port), ec);
    if (ec) {
        state.SkipWithError("Connection failed");
        return;
    }
    
    // Large buffer to test zero-copy efficiency
    std::vector<uint8_t> data(512 * 1024); // 512KB
    std::iota(data.begin(), data.end(), 0);
    
    size_t total_bytes = 0;
    
    for (auto _ : state) {
        // Use const_buffer for zero-copy semantics
        asio::const_buffer buffer(data.data(), data.size());
        size_t bytes_sent = asio::write(socket, buffer, ec);
        if (ec) {
            state.SkipWithError("Write failed");
            break;
        }
        total_bytes += bytes_sent;
    }
    
    socket.close();
    
    state.SetBytesProcessed(total_bytes);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ZeroCopyForwarding)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(50);

// Benchmark: Latency under load (measure response time)
static void BM_LatencyUnderLoad(benchmark::State& state) {
    SetupBenchmark();
    
    auto& io_context = g_io_pool->get_io_context();
    asio::ip::tcp::socket socket(io_context);
    asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    auto local_port = acceptor.local_endpoint().port();
    
    boost::system::error_code ec;
    socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), local_port), ec);
    if (ec) {
        state.SkipWithError("Connection failed");
        return;
    }
    
    std::vector<uint8_t> data(1024); // 1KB request
    std::fill(data.begin(), data.end(), 0x42);
    
    std::vector<double> latencies;
    latencies.reserve(state.max_iterations);
    
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        
        asio::write(socket, asio::buffer(data), ec);
        if (ec) {
            state.SkipWithError("Write failed");
            break;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        latencies.push_back(duration.count());
    }
    
    socket.close();
    
    // Calculate latency statistics
    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        double p50 = latencies[latencies.size() / 2];
        double p95 = latencies[(latencies.size() * 95) / 100];
        double p99 = latencies[(latencies.size() * 99) / 100];
        
        state.counters["latency_p50_us"] = p50;
        state.counters["latency_p95_us"] = p95;
        state.counters["latency_p99_us"] = p99;
    }
    
    state.SetBytesProcessed(state.iterations() * 1024);
}
BENCHMARK(BM_LatencyUnderLoad)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(1000);

// Main benchmark runner
BENCHMARK_MAIN();
