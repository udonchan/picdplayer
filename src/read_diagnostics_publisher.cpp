#include "read_diagnostics_publisher.hpp"
#include "diagnostics_json.hpp"
#include <stdexcept>
#include <utility>

DiagnosticPublication serialize_read_diagnostics(const DiagnosticCapture& capture) {
    using Json = nlohmann::json;
    const auto& read = capture.read;
    const Json snapshot{{"sequence", capture.sequence},
                        {"captured_at_unix_ms", capture.captured_at_unix_ms}};
    Json map{{"schema_version", 1}, {"session_id", read.session_id},
             {"stream_generation", read.stream_generation}, {"snapshot", snapshot},
             {"disc_map", diagnostic_disc_map(read.disc_map)}};
    DiagnosticPublication result{map.dump(), {}};
    if (capture.detail) {
        auto fields = diagnostic_fields({}, read, {});
        auto& values = fields["read"];
        Json history;
        history["schema_version"] = 1;
        history["session_id"] = read.session_id;
        history["stream_generation"] = read.stream_generation;
        history["snapshot"] = snapshot;
        history["history"] = std::move(values["history"]);
        history["disc_map"] = std::move(map["disc_map"]);
        result.history = history.dump();
    }
    return result;
}

ReadDiagnosticsPublisher::ReadDiagnosticsPublisher(Serialize serializer, std::chrono::milliseconds refresh)
    : serialize_(serializer ? std::move(serializer) : serialize_read_diagnostics), refresh_(refresh) {
    if (refresh_.count() <= 0) throw std::invalid_argument("invalid diagnostic refresh interval");
    thread_ = std::thread(&ReadDiagnosticsPublisher::run, this);
}
ReadDiagnosticsPublisher::~ReadDiagnosticsPublisher() {
    { std::lock_guard lock(mutex_); closing_ = true; pending_.reset(); }
    changed_.notify_all();
    thread_.join();
}
std::string ReadDiagnosticsPublisher::request(DiagnosticContext context, bool detail, const Capture& capture) {
    std::shared_ptr<const std::string> body;
    {
        std::lock_guard lock(mutex_);
        if (context != context_) {
            context_ = context;
            ++context_revision_;
            map_.reset(); history_.reset(); pending_.reset();
            map_requested_.reset(); history_requested_.reset();
        }
        body = detail ? history_ : map_;
        const auto now = std::chrono::steady_clock::now();
        auto& requested = detail ? history_requested_ : map_requested_;
        const bool covered = (active_ && active_revision_ == context_revision_ && active_context_ == context && (active_detail_ || !detail)) ||
            (pending_ && (pending_->detail || !detail));
        if (!covered && (!requested || now - *requested >= refresh_)) {
            auto read = capture(detail);
            if (read.stream_generation != context.stream_generation ||
                (context.disc_generation && (!read.disc_map || read.disc_map->disc_generation != context.disc_generation)))
                throw std::logic_error("diagnostic capture context mismatch");
            if (!context.disc_generation) read.disc_map.reset();
            if (read.recent_reads.size() > read_history_capacity ||
                (read.disc_map && read.disc_map->size > DiscReadMap::capacity))
                throw std::length_error("diagnostic capture exceeds capacity");
            const auto captured = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            pending_ = DiagnosticCapture{std::move(read), ++sequence_, captured, detail};
            requested = now;
            changed_.notify_one();
        }
    }
    return body ? *body : std::string{};
}
void ReadDiagnosticsPublisher::run() {
    for (;;) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return closing_ || pending_.has_value(); });
        if (closing_) return;
        auto capture = std::move(*pending_);
        pending_.reset();
        const auto context = context_;
        const auto revision = context_revision_;
        active_ = true; active_context_ = context; active_detail_ = capture.detail; active_revision_ = revision;
        lock.unlock();
        std::shared_ptr<const std::string> map, history;
        try {
            auto publication = serialize_(capture);
            constexpr auto maximum_bytes = 1024 * 1024;
            if (publication.map.size() > maximum_bytes || publication.history.size() > maximum_bytes)
                throw std::length_error("diagnostic publication exceeds byte limit");
            if (!publication.map.empty()) map = std::make_shared<const std::string>(std::move(publication.map));
            if (!publication.history.empty()) history = std::make_shared<const std::string>(std::move(publication.history));
        } catch (...) { /* A failed projection is unavailable, never invented evidence. */ }
        lock.lock();
        active_ = false;
        if (!closing_ && context == context_ && revision == context_revision_) {
            if (map && capture.sequence > map_sequence_) { map_ = std::move(map); map_sequence_ = capture.sequence; }
            if (history && capture.sequence > history_sequence_) { history_ = std::move(history); history_sequence_ = capture.sequence; }
        }
    }
}
