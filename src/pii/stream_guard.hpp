#ifndef WAYBLUR_STREAM_GUARD_HPP
#define WAYBLUR_STREAM_GUARD_HPP

#include "pii/pii_classifier.hpp"
#include "pii/pii_ocr.hpp"
#include "renderer/privacy_filter.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace wayblur {

// Sits between screen capture and the shared stream and guarantees that sensitive
// data is not sent in the clear.
//
//  * The frame is split into tiles. A tile is "verified" once OCR has read exactly its
//    current pixels. Unchanged tiles stay verified, so a static screen costs no OCR.
//  * Only changed rows ("bands") are sent to OCR.
//  * A frame is released to viewers once every tile that looks like text is verified.
//    If OCR is late (max_delay_ms) the frame is released anyway but with every
//    unverified text-like tile blurred, so slow detection never leaks content.
//
// Not thread-safe against itself: push()/poll() are meant for one capture thread.
class StreamGuard {
public:
    using Clock = std::chrono::steady_clock;
    using OcrFn = std::function<std::vector<OcrLine>(const GrayImage&)>;

    struct Options {
        int tile{32};              // tile edge in pixels
        int max_delay_ms{250};     // longest a frame may wait for verification
        size_t max_queue{8};       // frames held at most
        bool fail_closed{true};    // blur unverified text-like tiles when releasing late
        int pad_px{8};             // grow each detected region
        int blur_block{16};        // minimum mosaic block size
        int max_ocr_width{1920};   // wider crops are downscaled before OCR
        uint32_t kinds_mask{0xFFFFFFFFu};   // bit i enables PiiKind(i)
    };

    struct OutFrame {
        std::vector<uint8_t> data;
        int width{0};
        int height{0};
        int stride{0};
        std::string format;
        bool failed_closed{false};   // released late with unverified areas blurred
    };

    struct Stats {
        uint64_t frames_in{0};
        uint64_t frames_out{0};
        uint64_t frames_failed_closed{0};
        uint64_t ocr_bands{0};
        uint64_t ocr_pixels{0};
    };

    StreamGuard(Options opts, OcrFn ocr);
    ~StreamGuard();
    StreamGuard(const StreamGuard&) = delete;
    StreamGuard& operator=(const StreamGuard&) = delete;

    void start();
    void stop();

    void set_mode(BlurMode m) { mode_ = m; }
    BlurMode mode() const { return mode_; }
    void set_kinds_mask(uint32_t m) { kinds_mask_ = m; }
    // Window-based rules (app/domain policies); applied to every released frame.
    void set_extra_rules(std::vector<FilterRule> rules);

    // Frames must be 4 bytes per pixel (BGRA/BGRx/RGBA/RGBx).
    void push(const FrameBuffer& frame, Clock::time_point now);
    // Fills `out` with the newest frame that is safe to send. False if none is due yet.
    bool poll(Clock::time_point now, OutFrame& out);

    Stats stats() const;

private:
    struct Entry {
        std::vector<uint8_t> px;
        std::vector<uint64_t> hashes;   // one per tile
        int width{0}, height{0}, stride{0};
        std::string format;
        Clock::time_point arrival;
    };
    struct Region {
        BoundingBox box;
    };

    int tiles_x() const { return (width_ + opts_.tile - 1) / opts_.tile; }
    int tiles_y() const { return (height_ + opts_.tile - 1) / opts_.tile; }

    void hash_tiles(const FrameBuffer& f, std::vector<uint64_t>& out) const;
    bool tile_is_texty(const Entry& e, int tx, int ty) const;
    void render(const Entry& e, bool fail_closed_blur, OutFrame& out) const;
    bool entry_clean(const Entry& e) const;   // caller holds mu_
    void worker_loop();

    Options opts_;
    OcrFn ocr_;
    PiiClassifier classifier_;
    PrivacyFilter filter_;

    std::atomic<BlurMode> mode_{BlurMode::DataOnly};
    std::atomic<uint32_t> kinds_mask_{0xFFFFFFFFu};

    mutable std::mutex mu_;
    int width_{0}, height_{0};
    std::vector<uint64_t> verified_;   // per tile; 0 = never verified
    std::vector<Region> regions_;
    std::deque<std::shared_ptr<Entry>> queue_;
    std::vector<FilterRule> extra_rules_;
    Stats stats_;

    // Worker hand-off: always the newest frame that still has unverified tiles.
    std::condition_variable cv_;
    std::shared_ptr<Entry> job_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace wayblur

#endif // WAYBLUR_STREAM_GUARD_HPP
