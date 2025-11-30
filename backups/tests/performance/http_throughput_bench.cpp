#include <benchmark/benchmark.h>
#include "../../src/proxy/http_proxy.h"
#include "../../src/agent/agent_registry.h"
#include "../../src/storage/cache.h"
#include "../../src/models/tunnel.h"
#include <memory>
#include <random>

using namespace protogate;

// Setup fixtures
static std::shared_ptr<proxy::HTTPProxy> create_http_proxy() {
    auto tunnel_cache = std::make_shared<storage::Cache<std::string, models::Tunnel>>();
    auto agent_registry = std::make_shared<agent::AgentRegistry>(50);
    
    // Setup test tunnel
    models::Tunnel tunnel;
    tunnel.tunnel_id = "bench_api";
    tunnel.protocol = models::TunnelProtocol::HTTP;
    tunnel.target_host = "localhost";
    tunnel.target_port = 8080;
    tunnel.status = models::TunnelStatus::ACTIVE;
    
    tunnel_cache->put("bench_api", tunnel, std::chrono::hours(1));
    
    return std::make_shared<proxy::HTTPProxy>(tunnel_cache, agent_registry);
}

// Benchmark HTTP request parsing
static void BM_HTTPRequestParsing(benchmark::State& state) {
    std::string raw_request = 
        "GET /api/users/123 HTTP/1.1\r\n"
        "Host: api.tunnel.test.com\r\n"
        "Content-Type: application/json\r\n"
        "Authorization: Bearer tnl_test_token\r\n"
        "User-Agent: BenchmarkClient/1.0\r\n"
        "\r\n";
    
    for (auto _ : state) {
        auto parsed = proxy::HTTPProxy::parse_request(raw_request);
        benchmark::DoNotOptimize(parsed);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HTTPRequestParsing);

// Benchmark HTTP request parsing with large body
static void BM_HTTPRequestParsingWithBody(benchmark::State& state) {
    std::string body(state.range(0), 'x'); // Variable size body
    
    std::string raw_request = 
        "POST /api/data HTTP/1.1\r\n"
        "Host: api.tunnel.test.com\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" + body;
    
    for (auto _ : state) {
        auto parsed = proxy::HTTPProxy::parse_request(raw_request);
        benchmark::DoNotOptimize(parsed);
    }
    
    state.SetBytesProcessed(state.iterations() * raw_request.size());
}
BENCHMARK(BM_HTTPRequestParsingWithBody)->Range(1024, 1024*1024); // 1KB to 1MB

// Benchmark hostname to tunnel matching
static void BM_HostnameToTunnelMatching(benchmark::State& state) {
    auto http_proxy = create_http_proxy();
    
    for (auto _ : state) {
        std::string tunnel_id = http_proxy->match_tunnel("api.tunnel.test.com");
        benchmark::DoNotOptimize(tunnel_id);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HostnameToTunnelMatching);

// Benchmark HTTP request handling (without agent)
static void BM_HTTPRequestHandling(benchmark::State& state) {
    auto http_proxy = create_http_proxy();
    
    proxy::HTTPProxy::HTTPRequest request;
    request.method = "GET";
    request.path = "/api/users";
    request.version = "HTTP/1.1";
    request.host = "api.tunnel.test.com";
    request.client_ip = "192.168.1.100";
    
    for (auto _ : state) {
        http_proxy->handle_request(request,
            [](const std::string& response, bool error) {
                // No-op callback
            });
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HTTPRequestHandling);

// Benchmark concurrent request handling
static void BM_ConcurrentRequestHandling(benchmark::State& state) {
    auto http_proxy = create_http_proxy();
    
    for (auto _ : state) {
        state.PauseTiming();
        
        std::vector<proxy::HTTPProxy::HTTPRequest> requests(state.range(0));
        for (size_t i = 0; i < requests.size(); ++i) {
            requests[i].method = "GET";
            requests[i].path = "/api/users/" + std::to_string(i);
            requests[i].version = "HTTP/1.1";
            requests[i].host = "api.tunnel.test.com";
            requests[i].client_ip = "192.168.1.100";
        }
        
        state.ResumeTiming();
        
        for (const auto& request : requests) {
            http_proxy->handle_request(request,
                [](const std::string& response, bool error) {});
        }
    }
    
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ConcurrentRequestHandling)->Range(1, 1000);

// Benchmark IP allowlist validation
static void BM_IPAllowlistValidation(benchmark::State& state) {
    auto http_proxy = create_http_proxy();
    
    models::Tunnel tunnel;
    tunnel.tunnel_id = "secure_api";
    tunnel.ip_allowlist = {"192.168.1.0/24", "10.0.0.0/8", "172.16.0.0/12"};
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (auto _ : state) {
        std::string ip = std::to_string(dis(gen)) + "." + 
                        std::to_string(dis(gen)) + "." +
                        std::to_string(dis(gen)) + "." +
                        std::to_string(dis(gen));
        
        bool allowed = http_proxy->validate_ip_allowlist(tunnel, ip);
        benchmark::DoNotOptimize(allowed);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_IPAllowlistValidation);

// Benchmark HTTP response serialization
static void BM_HTTPResponseSerialization(benchmark::State& state) {
    proxy::HTTPProxy::HTTPResponse response;
    response.status_code = 200;
    response.status_message = "OK";
    response.headers["content-type"] = "application/json";
    response.headers["content-length"] = "100";
    response.body = std::string(100, 'x');
    
    for (auto _ : state) {
        std::string serialized = response.to_string();
        benchmark::DoNotOptimize(serialized);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HTTPResponseSerialization);

// Benchmark tunnel cache lookup
static void BM_TunnelCacheLookup(benchmark::State& state) {
    auto tunnel_cache = std::make_shared<storage::Cache<std::string, models::Tunnel>>();
    
    // Populate cache with test data
    for (int i = 0; i < state.range(0); ++i) {
        models::Tunnel tunnel;
        tunnel.tunnel_id = "tunnel_" + std::to_string(i);
        tunnel.protocol = models::TunnelProtocol::HTTP;
        tunnel.target_host = "localhost";
        tunnel.target_port = 8080;
        tunnel_cache->put(tunnel.tunnel_id, tunnel, std::chrono::hours(1));
    }
    
    for (auto _ : state) {
        auto tunnel = tunnel_cache->get("tunnel_" + std::to_string(state.range(0) / 2));
        benchmark::DoNotOptimize(tunnel);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TunnelCacheLookup)->Range(10, 10000);

BENCHMARK_MAIN();
