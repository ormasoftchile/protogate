#include "tracer.h"
#include "logger.h"
#include <random>
#include <iomanip>
#include <sstream>
#include <mutex>

namespace protogate {
namespace observability {

// Thread-local random generator for span/trace IDs
thread_local std::mt19937_64 rng(std::random_device{}());

// Static initialization
static std::once_flag tracer_init_flag;
static std::unique_ptr<Tracer> global_tracer;

//
// Span Implementation
//

Span::Span(const std::string& operation_name)
    : operation_name_(operation_name)
    , trace_id_(generate_trace_id())
    , span_id_(generate_span_id())
    , parent_span_id_(std::nullopt)
    , start_time_(std::chrono::system_clock::now())
    , status_(SpanStatus::UNSET)
    , ended_(false)
{
    Logger::instance().debug("Span started (root)",
        {{"operation", operation_name_},
         {"trace_id", trace_id_},
         {"span_id", span_id_}});
}

Span::Span(const std::string& operation_name,
           const TraceId& trace_id,
           const SpanId& parent_span_id)
    : operation_name_(operation_name)
    , trace_id_(trace_id)
    , span_id_(generate_span_id())
    , parent_span_id_(parent_span_id)
    , start_time_(std::chrono::system_clock::now())
    , status_(SpanStatus::UNSET)
    , ended_(false)
{
    Logger::instance().debug("Span started (child)",
        {{"operation", operation_name_},
         {"trace_id", trace_id_},
         {"span_id", span_id_},
         {"parent_span_id", parent_span_id}});
}

Span::~Span() {
    if (!ended_) {
        end();
    }
}

void Span::add_attribute(const std::string& key, const std::string& value) {
    if (!ended_) {
        attributes_[key] = value;
    }
}

void Span::add_attribute(const std::string& key, int64_t value) {
    if (!ended_) {
        attributes_[key] = std::to_string(value);
    }
}

void Span::add_attribute(const std::string& key, double value) {
    if (!ended_) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value;
        attributes_[key] = oss.str();
    }
}

void Span::add_attribute(const std::string& key, bool value) {
    if (!ended_) {
        attributes_[key] = value ? "true" : "false";
    }
}

void Span::add_event(const std::string& name,
                     const std::map<std::string, std::string>& attributes)
{
    if (!ended_) {
        events_.push_back({name, attributes});
        
        // Log event
        auto event_fields = attributes;
        event_fields["span_event"] = name;
        event_fields["trace_id"] = trace_id_;
        event_fields["span_id"] = span_id_;
        Logger::instance().debug("Span event", event_fields);
    }
}

void Span::set_status(SpanStatus status, const std::string& description) {
    if (!ended_) {
        status_ = status;
        status_description_ = description;
    }
}

void Span::end() {
    if (ended_) {
        return;
    }
    
    ended_ = true;
    end_time_ = std::chrono::system_clock::now();
    
    // Calculate duration
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time_ - start_time_).count();
    
    // Build log fields
    std::map<std::string, std::string> fields = attributes_;
    fields["operation"] = operation_name_;
    fields["trace_id"] = trace_id_;
    fields["span_id"] = span_id_;
    fields["duration_ms"] = std::to_string(duration_ms);
    
    if (parent_span_id_) {
        fields["parent_span_id"] = *parent_span_id_;
    }
    
    // Add status
    switch (status_) {
        case SpanStatus::OK:
            fields["status"] = "OK";
            break;
        case SpanStatus::ERROR:
            fields["status"] = "ERROR";
            break;
        case SpanStatus::UNSET:
        default:
            fields["status"] = "UNSET";
            break;
    }
    
    if (!status_description_.empty()) {
        fields["status_description"] = status_description_;
    }
    
    // Log span completion
    if (status_ == SpanStatus::ERROR) {
        Logger::instance().error("Span ended", fields);
    } else {
        Logger::instance().info("Span ended", fields);
    }
    
    // TODO: Export to OpenTelemetry backend / Azure Application Insights
    // auto exporter = get_span_exporter();
    // exporter->export_span(*this);
}

SpanId Span::generate_span_id() {
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t id = dist(rng);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << id;
    return oss.str();
}

TraceId Span::generate_trace_id() {
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t high = dist(rng);
    uint64_t low = dist(rng);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << high
        << std::setw(16) << low;
    return oss.str();
}

//
// Tracer Implementation
//

void Tracer::initialize(
    const std::string& service_name,
    const std::string& application_insights_connection_string)
{
    std::call_once(tracer_init_flag, [&]() {
        global_tracer = std::unique_ptr<Tracer>(new Tracer());
        global_tracer->service_name_ = service_name;
        global_tracer->app_insights_connection_string_ = application_insights_connection_string;
        global_tracer->enabled_ = true;
        
        Logger::instance().info("Tracer initialized",
            {{"service_name", service_name},
             {"app_insights_enabled", application_insights_connection_string.empty() ? "false" : "true"}});
        
        // TODO: Initialize OpenTelemetry SDK
        // auto exporter = std::make_shared<opentelemetry::exporter::otlp::OtlpHttpExporter>();
        // auto processor = std::make_shared<opentelemetry::sdk::trace::SimpleSpanProcessor>(exporter);
        // auto provider = std::make_shared<opentelemetry::sdk::trace::TracerProvider>(processor);
        // opentelemetry::trace::Provider::SetTracerProvider(provider);
    });
}

Tracer& Tracer::instance() {
    if (!global_tracer) {
        // Auto-initialize with defaults if not explicitly initialized
        initialize("protogate-server", "");
    }
    return *global_tracer;
}

std::unique_ptr<Span> Tracer::start_span(const std::string& operation_name) {
    if (!enabled_) {
        return nullptr;
    }
    
    return std::make_unique<Span>(operation_name);
}

std::unique_ptr<Span> Tracer::start_span(
    const std::string& operation_name,
    const TraceId& trace_id,
    const SpanId& parent_span_id)
{
    if (!enabled_) {
        return nullptr;
    }
    
    return std::make_unique<Span>(operation_name, trace_id, parent_span_id);
}

}  // namespace observability
}  // namespace protogate
