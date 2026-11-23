#ifndef WAYBLUR_CAPTURE_PIPELINE_HPP
#define WAYBLUR_CAPTURE_PIPELINE_HPP

#include "renderer/privacy_filter.hpp"
#include "pii/stream_guard.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>

#include <functional>
#include <memory>
#include <atomic>
#include <chrono>
#include <mutex>

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

    // Turn on the leak-proof stream guard. DataOnly needs OCR (returns false if it can't
    // start); FullScreen needs nothing. When no guard is enabled, only window rules apply.
    bool enable_protection(BlurMode mode = BlurMode::DataOnly, const char* ocr_lang = "eng");
    bool protection_active() const { return guard_ != nullptr; }
    // Switch at runtime. DataOnly is refused (returns false) if OCR is unavailable.
    bool set_blur_mode(BlurMode mode);
    BlurMode blur_mode() const { return guard_ ? guard_->mode() : BlurMode::DataOnly; }
    void set_pii_kinds_mask(uint32_t mask) { if (guard_) guard_->set_kinds_mask(mask); }
    StreamGuard::Stats guard_stats() const { return guard_ ? guard_->stats() : StreamGuard::Stats{}; }

    PipelineStats get_stats() const;

    // Process a synthetic or external buffer directly (for test mode & benchmark mode)
    void process_frame(FrameBuffer& frame);

private:
    struct pw_thread_loop* loop_{nullptr};
    struct pw_context* context_{nullptr};
    struct pw_core* core_{nullptr};
    struct pw_stream* stream_{nullptr}; // Input stream
    struct pw_stream* stream_out_{nullptr}; // Output stream
    struct spa_hook stream_listener_{};
    struct spa_hook stream_out_listener_{};

    mutable std::mutex rules_mu_;            // filter_rules_ is written by the app thread, read by PipeWire's
    std::vector<FilterRule> filter_rules_;
    PrivacyFilter filter_;

    std::unique_ptr<OcrEngine> ocr_;
    std::unique_ptr<StreamGuard> guard_;
    struct spa_source* timer_{nullptr};      // releases held frames even when the screen is static
    std::string frame_format_{"RGBA"};       // negotiated pixel layout

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frames_processed_{0};
    std::atomic<uint64_t> frames_dropped_{0};
    std::chrono::steady_clock::time_point start_time_;
    int frame_width_{1920};
    int frame_height_{1080};

    void emit_ready();
    static void on_timer(void* data, uint64_t expirations);
    static void on_process(void* data);
    static void on_param_changed(void* data, uint32_t id, const struct spa_pod* param);
    static void on_state_changed(void* data, enum pw_stream_state old_state, enum pw_stream_state new_state, const char* error);
};

} // namespace wayblur

#endif // WAYBLUR_CAPTURE_PIPELINE_HPP
