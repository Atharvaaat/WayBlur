#ifndef WAYBLUR_CAPTURE_PIPELINE_HPP
#define WAYBLUR_CAPTURE_PIPELINE_HPP

#include "renderer/privacy_filter.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>

#include <functional>
#include <memory>
#include <atomic>
#include <chrono>

namespace wayblur {

struct PipelineStats {
    uint64_t frames_processed{0};
    uint64_t frames_dropped{0};
    double avg_latency_ms{0.0};
    double current_fps{0.0};
};

class CapturePipeline {
public:
    CapturePipeline();
    ~CapturePipeline();

    bool init(int pipewire_fd = -1, uint32_t node_id = 0);
    bool start();
    void stop();

    void set_filter_rules(const std::vector<FilterRule>& rules);
    PipelineStats get_stats() const;

    // Process a synthetic or external buffer directly (for test mode & benchmark mode)
    void process_frame(FrameBuffer& frame);

private:
    struct pw_thread_loop* loop_{nullptr};
    struct pw_context* context_{nullptr};
    struct pw_core* core_{nullptr};
    struct pw_stream* stream_{nullptr};
    struct spa_hook stream_listener_{};

    std::vector<FilterRule> filter_rules_;
    PrivacyFilter filter_;

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frames_processed_{0};
    std::atomic<uint64_t> frames_dropped_{0};
    std::chrono::steady_clock::time_point start_time_;

    static void on_process(void* data);
    static void on_state_changed(void* data, enum pw_stream_state old_state, enum pw_stream_state new_state, const char* error);
};

} // namespace wayblur

#endif // WAYBLUR_CAPTURE_PIPELINE_HPP
