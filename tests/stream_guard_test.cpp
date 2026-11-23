#include "pii/stream_guard.hpp"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace wayblur;
using Clock = StreamGuard::Clock;
using ms = std::chrono::milliseconds;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

constexpr int W = 640, H = 384;
constexpr int STRIDE = W * 4;

// Fake OCR: "PII" is any pixel of gray value 77. Returns one email word spanning those pixels.
static std::atomic<int> g_ocr_sleep_ms{5};
static std::atomic<int> g_ocr_calls{0};
static std::vector<OcrLine> fake_ocr(const GrayImage& g) {
    g_ocr_calls++;
    std::this_thread::sleep_for(ms(g_ocr_sleep_ms));
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    for (int y = 0; y < g.height; ++y)
        for (int x = 0; x < g.width; ++x)
            if (g.pixels[static_cast<size_t>(y) * g.width + x] == 77) {
                x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
            }
    if (x1 < 0) return {};
    OcrLine l;
    l.words.push_back({"jane@example.com", {x0, y0, x1 - x0 + 1, y1 - y0 + 1}});
    return {l};
}

struct Rect { int x, y, w, h; };

static void stripes(std::vector<uint8_t>& px, Rect r, uint8_t gray) {
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) {
            uint8_t v = ((x / 2) % 2 == 0) ? gray : 240;   // vertical stripes: strong transitions
            uint8_t* p = &px[static_cast<size_t>(y) * STRIDE + x * 4];
            p[0] = p[1] = p[2] = v; p[3] = 255;
        }
}

static std::vector<uint8_t> frame(const Rect* pii, const Rect* decoy, const Rect* extra = nullptr) {
    std::vector<uint8_t> px(static_cast<size_t>(STRIDE) * H, 240);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    if (pii) stripes(px, *pii, 77);
    if (decoy) stripes(px, *decoy, 150);
    if (extra) stripes(px, *extra, 150);
    return px;
}

static FrameBuffer fb(std::vector<uint8_t>& px) {
    FrameBuffer f; f.data = px.data(); f.width = W; f.height = H; f.stride = STRIDE; f.format = "BGRA";
    return f;
}

static size_t diff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, Rect r) {
    size_t n = 0;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x * 4; x < (r.x + r.w) * 4; ++x)
            if (a[static_cast<size_t>(y) * STRIDE + x] != b[static_cast<size_t>(y) * STRIDE + x]) ++n;
    return n;
}

static bool blurred(const std::vector<uint8_t>& orig, const StreamGuard::OutFrame& o, Rect r) {
    return diff(orig, o.data, r) > static_cast<size_t>(r.w) * r.h;   // substantially changed
}
static bool untouched(const std::vector<uint8_t>& orig, const StreamGuard::OutFrame& o, Rect r) {
    return diff(orig, o.data, r) == 0;
}

// Pump poll() until a frame is released or timeout; returns every frame released.
static std::vector<StreamGuard::OutFrame> drain(StreamGuard& g, int timeout_ms, int want = 1) {
    std::vector<StreamGuard::OutFrame> got;
    auto end = Clock::now() + ms(timeout_ms);
    while (Clock::now() < end && static_cast<int>(got.size()) < want) {
        StreamGuard::OutFrame o;
        if (g.poll(Clock::now(), o)) got.push_back(std::move(o));
        else std::this_thread::sleep_for(ms(2));
    }
    return got;
}

int main() {
    const Rect PII{64, 96, 256, 24};
    const Rect DECOY{64, 288, 256, 24};

    // ---- 1. Fast OCR: PII blurred, harmless text left alone, never leaks. ----
    {
        g_ocr_sleep_ms = 5;
        StreamGuard g({}, fake_ocr);
        g.start();
        auto px = frame(&PII, &DECOY);
        auto f = fb(px);
        g.push(f, Clock::now());
        auto out = drain(g, 1500);
        CHECK(out.size() == 1);
        if (!out.empty()) {
            CHECK(blurred(px, out[0], PII));
            CHECK(untouched(px, out[0], DECOY));
            CHECK(!out[0].failed_closed);
        }
        g.stop();
    }

    // ---- 2. Slow OCR (400ms) with a 100ms deadline: still never leaks (fail-closed). ----
    {
        g_ocr_sleep_ms = 400;
        StreamGuard::Options o; o.max_delay_ms = 100;
        StreamGuard g(o, fake_ocr);
        g.start();
        auto px = frame(&PII, &DECOY);
        auto f = fb(px);
        auto t0 = Clock::now();
        g.push(f, t0);
        // Keep the frames coming while OCR is busy, as a live stream would.
        std::vector<StreamGuard::OutFrame> all;
        while (Clock::now() - t0 < ms(900)) {
            g.push(f, Clock::now());
            StreamGuard::OutFrame out;
            while (g.poll(Clock::now(), out)) all.push_back(std::move(out));
            std::this_thread::sleep_for(ms(10));
        }
        CHECK(all.size() > 3);
        int leaks = 0, failed_closed = 0;
        for (auto& o2 : all) {
            if (!blurred(px, o2, PII)) ++leaks;
            if (o2.failed_closed) ++failed_closed;
        }
        std::printf("  slow-OCR run: %zu frames released, %d fail-closed, %d leaked\n", all.size(), failed_closed, leaks);
        CHECK(leaks == 0);
        CHECK(failed_closed >= 1);                     // the deadline did kick in
        CHECK(untouched(px, all.back(), DECOY));       // and it relaxes once OCR has caught up
        g.stop();
    }

    // ---- 3. Static screen costs nothing after the first read. ----
    {
        g_ocr_sleep_ms = 5;
        StreamGuard g({}, fake_ocr);
        g.start();
        auto px = frame(&PII, &DECOY);
        auto f = fb(px);
        g_ocr_calls = 0;
        for (int i = 0; i < 20; ++i) {
            g.push(f, Clock::now());
            std::this_thread::sleep_for(ms(5));
        }
        drain(g, 500);
        std::this_thread::sleep_for(ms(100));
        auto st = g.stats();
        std::printf("  static screen: %llu OCR band(s) for 20 identical frames\n", (unsigned long long)st.ocr_bands);
        CHECK(st.ocr_bands == 1);
        CHECK(g_ocr_calls == 1);

        // ---- 4. A small edit re-reads only a thin band, not the screen. ----
        const Rect EDIT{64, 200, 40, 12};
        auto px2 = frame(&PII, &DECOY, &EDIT);
        auto f2 = fb(px2);
        auto before = g.stats();
        auto t0 = Clock::now();
        g.push(f2, t0);
        auto out = drain(g, 1500);
        auto latency = std::chrono::duration_cast<ms>(Clock::now() - t0).count();
        auto after = g.stats();
        uint64_t px_read = after.ocr_pixels - before.ocr_pixels;
        std::printf("  edit: %llu band, %llu px read (%.0f%% of frame), released in ~%lld ms\n",
                    (unsigned long long)(after.ocr_bands - before.ocr_bands), (unsigned long long)px_read,
                    100.0 * px_read / (W * H), (long long)latency);
        CHECK(after.ocr_bands - before.ocr_bands == 1);
        // Edit sits in one 32px tile row; the band adds one row of context each side = 96px.
        CHECK(px_read <= static_cast<uint64_t>(W) * 96);
        CHECK(!out.empty() && blurred(px2, out[0], PII));
        CHECK(latency < 200);

        // ---- 5. PII scrolls to a new place: old spot clears, new spot is blurred. ----
        const Rect MOVED{64, 192, 256, 24};
        auto px3 = frame(&MOVED, &DECOY);
        auto f3 = fb(px3);
        g.push(f3, Clock::now());
        auto out3 = drain(g, 1500);
        CHECK(!out3.empty());
        if (!out3.empty()) {
            CHECK(blurred(px3, out3[0], MOVED));
            CHECK(untouched(px3, out3[0], Rect{64, 96, 256, 24}));   // old position is plain again
        }
        g.stop();
    }

    // ---- 6. Whole-screen mode: everything blurred, OCR never used. ----
    {
        g_ocr_calls = 0;
        StreamGuard g({}, fake_ocr);
        g.set_mode(BlurMode::FullScreen);
        g.start();
        auto px = frame(&PII, &DECOY);
        auto f = fb(px);
        g.push(f, Clock::now());
        auto out = drain(g, 500);
        CHECK(out.size() == 1);
        if (!out.empty()) {
            CHECK(blurred(px, out[0], PII));
            CHECK(blurred(px, out[0], DECOY));
        }
        CHECK(g_ocr_calls == 0);
        g.stop();
    }

    // ---- 7. Disabling a data type stops it being blurred. ----
    {
        g_ocr_sleep_ms = 5;
        StreamGuard g({}, fake_ocr);
        g.set_kinds_mask(1u << static_cast<unsigned>(PiiKind::CreditCard));   // email not enabled
        g.start();
        auto px = frame(&PII, &DECOY);
        auto f = fb(px);
        g.push(f, Clock::now());
        auto out = drain(g, 1500);
        CHECK(!out.empty() && untouched(px, out[0], PII));
        g.stop();
    }

    if (g_failures == 0) std::puts("stream_guard_test: all checks passed");
    return g_failures == 0 ? 0 : 1;
}
