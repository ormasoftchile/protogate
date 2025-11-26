#pragma once

#include <string>
#include <map>
#include <memory>
#include <chrono>
#include <optional>
#include <vector>

namespace protogate {
namespace observability {

/**
 * @brief Span ID type (128-bit represented as hex string)
 */
using SpanId = std::string;

/**
 * @brief Trace ID type (256-bit represented as hex string)
 */
using TraceId = std::string;

/**
 * @brief Span status codes
 */
enum class SpanStatus {
    UNSET = 0,
    OK = 1,
    ERROR = 2
};

/**
 * @brief Distributed tracing span
 * 
 * Represents a unit of work in distributed tracing. Spans can be nested
 * to represent hierarchical relationships. Integrates with Azure Monitor
 * Application Insights distributed tracing.
 */
class Span {
public:
    /**
     * @brief Create root span (new trace)
     */
    explicit Span(const std::string& operation_name);
    
    /**
     * @brief Create child span
     */
    Span(const std::string& operation_name,
         const TraceId& trace_id,
         const SpanId& parent_span_id);
    
    ~Span();
    
    // Non-copyable but movable
    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;
    Span(Span&&) = default;
    Span& operator=(Span&&) = default;
    
    /**
     * @brief Add attribute to span
     */
    void add_attribute(const std::string& key, const std::string& value);
    void add_attribute(const std::string& key, int64_t value);
    void add_attribute(const std::string& key, double value);
    void add_attribute(const std::string& key, bool value);
    
    /**
     * @brief Add event to span (timestamped log entry)
     */
    void add_event(const std::string& name,
                   const std::map<std::string, std::string>& attributes = {});
    
    /**
     * @brief Set span status
     */
    void set_status(SpanStatus status, const std::string& description = "");
    
    /**
     * @brief Mark span as complete (automatically called by destructor)
     */
    void end();
    
    /**
     * @brief Get trace ID
     */
    const TraceId& trace_id() const { return trace_id_; }
    
    /**
     * @brief Get span ID
     */
    const SpanId& span_id() const { return span_id_; }
    
    /**
     * @brief Get parent span ID (empty if root span)
     */
    const std::optional<SpanId>& parent_span_id() const { return parent_span_id_; }
    
    /**
     * @brief Check if span is recording (not ended)
     */
    bool is_recording() const { return !ended_; }

private:
    std::string operation_name_;
    TraceId trace_id_;
    SpanId span_id_;
    std::optional<SpanId> parent_span_id_;
    
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
    
    std::map<std::string, std::string> attributes_;
    std::vector<std::pair<std::string, std::map<std::string, std::string>>> events_;
    
    SpanStatus status_;
    std::string status_description_;
    
    bool ended_;
    
    // Generate random IDs
    static SpanId generate_span_id();
    static TraceId generate_trace_id();
};

/**
 * @brief Tracer for creating spans
 * 
 * Thread-safe tracer that creates spans for distributed tracing.
 * Integrates with Azure Monitor Application Insights when configured.
 */
class Tracer {
public:
    /**
     * @brief Initialize global tracer
     * @param service_name Name of the service (e.g., "protogate-server")
     * @param application_insights_connection_string Azure App Insights connection string
     */
    static void initialize(
        const std::string& service_name,
        const std::string& application_insights_connection_string = "");
    
    /**
     * @brief Get singleton tracer instance
     */
    static Tracer& instance();
    
    /**
     * @brief Create a new root span (starts a new trace)
     */
    std::unique_ptr<Span> start_span(const std::string& operation_name);
    
    /**
     * @brief Create a child span
     */
    std::unique_ptr<Span> start_span(
        const std::string& operation_name,
        const TraceId& trace_id,
        const SpanId& parent_span_id);
    
    /**
     * @brief Get service name
     */
    const std::string& service_name() const { return service_name_; }
    
    /**
     * @brief Check if tracing is enabled
     */
    bool is_enabled() const { return enabled_; }

private:
    Tracer() = default;
    
    std::string service_name_;
    std::string app_insights_connection_string_;
    bool enabled_;
    
    // TODO: Add OpenTelemetry SDK integration
    // std::shared_ptr<opentelemetry::trace::TracerProvider> tracer_provider_;
    // std::shared_ptr<opentelemetry::trace::Tracer> tracer_;
};

/**
 * @brief RAII helper for automatic span lifecycle management
 * 
 * Usage:
 *   {
 *     auto span = ScopedSpan("operation_name");
 *     span.add_attribute("user_id", "123");
 *     // Do work...
 *   }  // Span automatically ended
 */
class ScopedSpan {
public:
    explicit ScopedSpan(const std::string& operation_name)
        : span_(Tracer::instance().start_span(operation_name)) {}
    
    ScopedSpan(const std::string& operation_name,
               const TraceId& trace_id,
               const SpanId& parent_span_id)
        : span_(Tracer::instance().start_span(operation_name, trace_id, parent_span_id)) {}
    
    ~ScopedSpan() {
        if (span_ && span_->is_recording()) {
            span_->end();
        }
    }
    
    // Provide access to underlying span
    Span& operator*() { return *span_; }
    Span* operator->() { return span_.get(); }
    
    // Non-copyable
    ScopedSpan(const ScopedSpan&) = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;
    
    // Movable
    ScopedSpan(ScopedSpan&&) = default;
    ScopedSpan& operator=(ScopedSpan&&) = default;

private:
    std::unique_ptr<Span> span_;
};

}  // namespace observability
}  // namespace protogate
