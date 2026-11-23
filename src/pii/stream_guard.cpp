#include "pii/stream_guard.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace wayblur {

namespace {

bool intersects(const BoundingBox& a, const BoundingBox& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
}

BoundingBox unite(const BoundingBox& a, const BoundingBox& b) {
    int x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    int x1 = std::max(a.x + a.width, b.x + b.width);
    int y1 = std::max(a.y + a.height, b.y + b.height);
    return {x0, y0, x1 - x0, y1 - y0};
}

// Mosaic block big enough that the region is at most ~2 blocks tall, i.e. unreadable.
int block_for(const BoundingBox& b, int min_block) {
    return std::max(min_block, b.height * 2 / 3);
}

} // namespace

StreamGuard::StreamGuard(Options opts, OcrFn ocr)
    : opts_(opts), ocr_(std::move(ocr)) {
    kinds_mask_ = opts.kinds_mask;
}

StreamGuard::~StreamGuard() { stop(); }

void StreamGuard::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this] { worker_loop(); });
}

void StreamGuard::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void StreamGuard::set_extra_rules(std::vector<FilterRule> rules) {
    std::lock_guard<std::mutex> lk(mu_);
    extra_rules_ = std::move(rules);
}

StreamGuard::Stats StreamGuard::stats() const {
    std::lock_guard<std::mutex> lk(mu_);
    return stats_;
}

void StreamGuard::hash_tiles(const FrameBuffer& f, std::vector<uint64_t>& out) const {
    const int T = opts_.tile;
    const int nx = (f.width + T - 1) / T, ny = (f.height + T - 1) / T;   // from the frame, not width_
    out.assign(static_cast<size_t>(nx) * ny, 0);
    // The 4th byte of each pixel is alpha/padding and may hold garbage (BGRx); ignore it.
    constexpr uint64_t kMask = 0x00FFFFFF00FFFFFFULL;
    for (int ty = 0; ty < ny; ++ty) {
        const int y0 = ty * T, y1 = std::min(f.height, y0 + T);
        for (int tx = 0; tx < nx; ++tx) {
            const int x0 = tx * T, x1 = std::min(f.width, x0 + T);
            const size_t bytes = static_cast<size_t>(x1 - x0) * 4;
            uint64_t h = 1469598103934665603ULL;
            for (int y = y0; y < y1; ++y) {
                const uint8_t* p = f.data + static_cast<size_t>(y) * f.stride + static_cast<size_t>(x0) * 4;
                size_t i = 0;
                for (; i + 8 <= bytes; i += 8) {
                    uint64_t w;
                    std::memcpy(&w, p + i, 8);
                    h = (h ^ (w & kMask)) * 1099511628211ULL;
                    h ^= h >> 31;
                }
                if (i + 4 <= bytes) {
                    uint32_t w;
                    std::memcpy(&w, p + i, 4);
                    h = (h ^ (w & 0x00FFFFFFu)) * 1099511628211ULL;
                }
            }
            out[static_cast<size_t>(ty) * nx + tx] = h | 1;   // never 0 (0 means "unverified")
        }
    }
}

bool StreamGuard::tile_is_texty(const Entry& e, int tx, int ty) const {
    const int T = opts_.tile;
    const int x0 = tx * T, x1 = std::min(e.width, x0 + T);
    const int y0 = ty * T, y1 = std::min(e.height, y0 + T);
    int strong = 0;
    // Text is dense in sharp horizontal transitions; flat fills and smooth gradients are not.
    for (int y = y0; y < y1; y += 2) {
        const uint8_t* p = e.px.data() + static_cast<size_t>(y) * e.stride;
        int prev = p[static_cast<size_t>(x0) * 4 + 1];
        for (int x = x0 + 1; x < x1; ++x) {
            int g = p[static_cast<size_t>(x) * 4 + 1];
            if (std::abs(g - prev) > 24) ++strong;
            prev = g;
        }
    }
    return strong >= 4;
}

bool StreamGuard::entry_clean(const Entry& e) const {
    if (!opts_.fail_closed) return true;
    const int nx = tiles_x(), ny = tiles_y();
    for (int ty = 0; ty < ny; ++ty) {
        for (int tx = 0; tx < nx; ++tx) {
            const size_t t = static_cast<size_t>(ty) * nx + tx;
            if (e.hashes[t] != verified_[t] && tile_is_texty(e, tx, ty)) return false;
        }
    }
    return true;
}

void StreamGuard::push(const FrameBuffer& frame, Clock::time_point now) {
    if (!frame.data || frame.width <= 0 || frame.height <= 0) return;

    auto e = std::make_shared<Entry>();
    e->width = frame.width;
    e->height = frame.height;
    e->stride = frame.stride;
    e->format = frame.format;
    e->arrival = now;
    e->px.assign(frame.data, frame.data + static_cast<size_t>(frame.stride) * frame.height);

    FrameBuffer view = frame;
    view.data = e->px.data();
    const bool screen = mode_ == BlurMode::FullScreen;
    if (!screen) hash_tiles(view, e->hashes);

    std::lock_guard<std::mutex> lk(mu_);
    if (frame.width != width_ || frame.height != height_) {
        width_ = frame.width;
        height_ = frame.height;
        verified_.assign(static_cast<size_t>(tiles_x()) * tiles_y(), 0);
        regions_.clear();
        queue_.clear();
    }
    if (screen && e->hashes.empty()) e->hashes.assign(verified_.size(), 0);

    queue_.push_back(e);
    stats_.frames_in++;

    if (!screen) {
        bool any = false;
        for (size_t t = 0; t < verified_.size() && !any; ++t) any = e->hashes[t] != verified_[t];
        if (any) {
            job_ = e;   // replaces any older pending job: the worker always reads the newest pixels
            cv_.notify_one();
        }
    }
}

void StreamGuard::render(const Entry& e, bool fail_closed_blur, OutFrame& out) const {
    out.data = e.px;
    out.width = e.width;
    out.height = e.height;
    out.stride = e.stride;
    out.format = e.format;
    out.failed_closed = fail_closed_blur;

    FrameBuffer fb;
    fb.data = out.data.data();
    fb.width = e.width;
    fb.height = e.height;
    fb.stride = e.stride;
    fb.format = e.format;

    // `filter_` is stateless; const_cast keeps render() usable from const contexts.
    auto& filter = const_cast<PrivacyFilter&>(filter_);

    for (const auto& r : regions_) {
        filter.apply_pixelate(fb, r.box, block_for(r.box, opts_.blur_block));
    }

    if (fail_closed_blur) {
        const int T = opts_.tile, nx = tiles_x(), ny = tiles_y();
        for (int ty = 0; ty < ny; ++ty) {
            int run_start = -1;
            for (int tx = 0; tx <= nx; ++tx) {
                bool bad = false;
                if (tx < nx) {
                    const size_t t = static_cast<size_t>(ty) * nx + tx;
                    bad = e.hashes[t] != verified_[t] && tile_is_texty(e, tx, ty);
                }
                if (bad && run_start < 0) run_start = tx;
                if (!bad && run_start >= 0) {
                    BoundingBox b{run_start * T, ty * T, (tx - run_start) * T, T};
                    filter.apply_pixelate(fb, b, std::max(opts_.blur_block, T / 2));
                    run_start = -1;
                }
            }
        }
    }

    if (!extra_rules_.empty()) filter.apply_filter(fb, extra_rules_);
}

bool StreamGuard::poll(Clock::time_point now, OutFrame& out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (queue_.empty()) return false;

    if (mode_ == BlurMode::FullScreen) {
        const Entry& e = *queue_.back();
        out.data = e.px;
        out.width = e.width;
        out.height = e.height;
        out.stride = e.stride;
        out.format = e.format;
        out.failed_closed = false;
        FrameBuffer fb{out.data.data(), e.width, e.height, e.stride, e.format};
        filter_.apply_pixelate(fb, {0, 0, e.width, e.height}, std::max(opts_.blur_block, 24));
        queue_.clear();
        stats_.frames_out++;
        return true;
    }

    // Newest frame that is already safe wins; anything older is skipped.
    for (size_t i = queue_.size(); i-- > 0;) {
        if (entry_clean(*queue_[i])) {
            render(*queue_[i], false, out);
            queue_.erase(queue_.begin(), queue_.begin() + static_cast<long>(i) + 1);
            stats_.frames_out++;
            return true;
        }
    }

    const auto& head = *queue_.front();
    const bool late = (now - head.arrival) >= std::chrono::milliseconds(opts_.max_delay_ms) ||
                      queue_.size() > opts_.max_queue;
    if (!late) return false;

    render(head, true, out);
    queue_.pop_front();
    stats_.frames_out++;
    stats_.frames_failed_closed++;
    return true;
}

void StreamGuard::worker_loop() {
    while (running_) {
        std::shared_ptr<Entry> e;
        std::vector<uint64_t> snapshot;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return !running_ || job_; });
            if (!running_) return;
            e = std::move(job_);
            job_.reset();
            if (e->width != width_ || e->height != height_) continue;
            snapshot = verified_;
        }

        const int T = opts_.tile;
        const int nx = (e->width + T - 1) / T, ny = (e->height + T - 1) / T;

        // Which tiles still need reading, and which tile rows that touches.
        std::vector<char> todo(snapshot.size(), 0);
        std::vector<char> row_todo(static_cast<size_t>(ny), 0);
        for (int ty = 0; ty < ny; ++ty) {
            for (int tx = 0; tx < nx; ++tx) {
                const size_t t = static_cast<size_t>(ty) * nx + tx;
                if (e->hashes[t] != snapshot[t]) { todo[t] = 1; row_todo[ty] = 1; }
            }
        }

        struct BandResult {
            int r0, r1;
            bool ok;
            std::vector<BoundingBox> boxes;
            uint64_t pixels;
        };
        std::vector<BandResult> results;

        for (int r = 0; r < ny;) {
            if (!row_todo[r]) { ++r; continue; }
            int r1 = r;
            while (r1 + 1 < ny && row_todo[r1 + 1]) ++r1;

            // One tile row of context above and below so text clipped by the band is whole.
            const int y_from = std::max(0, (r - 1) * T);
            const int y_to = std::min(e->height, (r1 + 2) * T);

            BandResult br{r, r1, false, {}, 0};
            FrameBuffer crop;
            crop.data = e->px.data() + static_cast<size_t>(y_from) * e->stride;
            crop.width = e->width;
            crop.height = y_to - y_from;
            crop.stride = e->stride;
            crop.format = e->format;

            double scale = 1.0;
            GrayImage gray = to_gray(crop, opts_.max_ocr_width, scale);
            br.pixels = static_cast<uint64_t>(gray.width) * gray.height;
            try {
                auto lines = ocr_ ? ocr_(gray) : std::vector<OcrLine>{};
                const double inv = scale > 0.0 ? 1.0 / scale : 1.0;
                const uint32_t mask = kinds_mask_;
                for (const auto& line : lines) {
                    for (const auto& m : classifier_.scan_line(line)) {
                        if (!(mask & (1u << static_cast<unsigned>(m.kind)))) continue;
                        BoundingBox b;
                        b.x = static_cast<int>(std::floor(m.box.x * inv)) - opts_.pad_px;
                        b.y = static_cast<int>(std::floor(m.box.y * inv)) + y_from - opts_.pad_px;
                        b.width = static_cast<int>(std::ceil(m.box.width * inv)) + 2 * opts_.pad_px;
                        b.height = static_cast<int>(std::ceil(m.box.height * inv)) + 2 * opts_.pad_px;
                        br.boxes.push_back(b);
                    }
                }
                br.ok = true;
            } catch (const std::exception& ex) {
                std::cerr << "[WayBlur:Guard] OCR failed: " << ex.what() << "\n";
            }
            results.push_back(std::move(br));
            r = r1 + 1;
        }

        std::lock_guard<std::mutex> lk(mu_);
        if (e->width != width_ || e->height != height_) continue;   // resolution changed meanwhile
        for (auto& br : results) {
            stats_.ocr_bands++;
            stats_.ocr_pixels += br.pixels;
            if (!br.ok) continue;   // stays unverified -> fail-closed

            const BoundingBox core{0, br.r0 * T, e->width, std::min(e->height, (br.r1 + 1) * T) - br.r0 * T};
            regions_.erase(std::remove_if(regions_.begin(), regions_.end(),
                               [&](const Region& g) { return intersects(g.box, core); }),
                           regions_.end());
            for (auto& b : br.boxes) {
                bool merged = false;
                for (auto& g : regions_) {
                    if (intersects(g.box, b)) { g.box = unite(g.box, b); merged = true; break; }
                }
                if (!merged) regions_.push_back({b});
            }
            for (int ty = br.r0; ty <= br.r1; ++ty) {
                for (int tx = 0; tx < nx; ++tx) {
                    const size_t t = static_cast<size_t>(ty) * nx + tx;
                    if (todo[t]) verified_[t] = e->hashes[t];
                }
            }
        }
    }
}

} // namespace wayblur
