// "Recorder" test: stream a real screen through StreamGuard with real Tesseract and capture
// EVERY frame released to viewers. No released frame may show PII, ever — including the
// first frames, before detection has finished.
//
// Usage: guard_real_ocr_test screen.pgm
//   (tests/make_fixture.py makes it; row layout: y=120 email, 200 card, 280 phone, 360 key,
//    520 PAN are PII; y=40 and y=440 are harmless)
#include "pii/stream_guard.hpp"

#include <cstdio>
#include <fstream>
#include <thread>

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

static bool read_pgm(const char* path, std::vector<uint8_t>& px, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    std::string magic; int maxv;
    if (!(f >> magic >> w >> h >> maxv) || magic != "P5") return false;
    f.get();
    px.resize(static_cast<size_t>(w) * h);
    f.read(reinterpret_cast<char*>(px.data()), px.size());
    return static_cast<size_t>(f.gcount()) == px.size();
}

static size_t diff_rows(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int stride, int y0, int y1) {
    size_t n = 0;
    for (int y = y0; y < y1; ++y)
        for (int i = 0; i < stride; ++i)
            if (a[static_cast<size_t>(y) * stride + i] != b[static_cast<size_t>(y) * stride + i]) ++n;
    return n;
}

static void run(const char* label, const std::vector<uint8_t>& gray, int w, int h, bool dark) {
    const int stride = w * 4;
    std::vector<uint8_t> orig(static_cast<size_t>(stride) * h);
    for (size_t i = 0; i < gray.size(); ++i) {
        uint8_t v = dark ? static_cast<uint8_t>(255 - gray[i]) : gray[i];
        orig[i * 4 + 0] = orig[i * 4 + 1] = orig[i * 4 + 2] = v;
        orig[i * 4 + 3] = 255;
    }

    OcrEngine ocr;
    if (!ocr.init("eng")) { std::fprintf(stderr, "OCR unavailable\n"); ++g_failures; return; }
    StreamGuard g({}, [&](const GrayImage& img) { return ocr.recognize(img); });
    g.start();

    const int pii_rows[][2] = {{120, 150}, {200, 230}, {280, 310}, {360, 390}, {520, 550}};
    const int benign_rows[][2] = {{40, 70}, {440, 470}};
    // Substantial change = the row is visibly different from the original pixels.
    const size_t kBlurred = 3000;

    FrameBuffer f; f.data = orig.data(); f.width = w; f.height = h; f.stride = stride; f.format = "BGRA";

    int released = 0, leaks = 0, fail_closed = 0;
    long first_clear_ms = -1;
    auto t0 = Clock::now();
    // A screen share delivers ~30 frames/s; a recorder keeps everything it is sent.
    while (Clock::now() - t0 < ms(3000)) {
        g.push(f, Clock::now());
        StreamGuard::OutFrame out;
        while (g.poll(Clock::now(), out)) {
            ++released;
            if (out.failed_closed) ++fail_closed;
            for (auto& r : pii_rows)
                if (diff_rows(orig, out.data, stride, r[0], r[1]) < kBlurred) { ++leaks; break; }
            if (first_clear_ms < 0 && diff_rows(orig, out.data, stride, 40, 70) == 0 &&
                diff_rows(orig, out.data, stride, 440, 470) == 0) {
                first_clear_ms = std::chrono::duration_cast<ms>(Clock::now() - t0).count();
            }
        }
        std::this_thread::sleep_for(ms(33));
    }

    // Settled state: harmless text readable, PII blurred.
    g.push(f, Clock::now());
    StreamGuard::OutFrame last;
    for (int i = 0; i < 100 && !g.poll(Clock::now(), last); ++i) std::this_thread::sleep_for(ms(5));
    for (auto& r : benign_rows) CHECK(diff_rows(orig, last.data, stride, r[0], r[1]) == 0);
    for (auto& r : pii_rows) CHECK(diff_rows(orig, last.data, stride, r[0], r[1]) >= kBlurred);

    auto st = g.stats();
    std::printf("%-6s: %d frames released to viewers, %d leaked PII, %d released fail-closed; "
                "harmless text first readable after %ld ms; OCR bands %llu\n",
                label, released, leaks, fail_closed, first_clear_ms, (unsigned long long)st.ocr_bands);
    CHECK(released > 20);
    CHECK(leaks == 0);
    CHECK(first_clear_ms >= 0);

    // Whole-screen mode: nothing readable anywhere, no OCR cost.
    g.set_mode(BlurMode::FullScreen);
    g.push(f, Clock::now());
    StreamGuard::OutFrame full;
    for (int i = 0; i < 100 && !g.poll(Clock::now(), full); ++i) std::this_thread::sleep_for(ms(5));
    for (auto& r : benign_rows) CHECK(diff_rows(orig, full.data, stride, r[0], r[1]) >= kBlurred);
    for (auto& r : pii_rows) CHECK(diff_rows(orig, full.data, stride, r[0], r[1]) >= kBlurred);
    g.stop();
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: guard_real_ocr_test screen.pgm\n"); return 2; }
    std::vector<uint8_t> gray; int w = 0, h = 0;
    if (!read_pgm(argv[1], gray, w, h)) { std::fprintf(stderr, "bad fixture\n"); return 2; }
    run("light", gray, w, h, false);
    run("dark", gray, w, h, true);
    if (g_failures == 0) std::puts("guard_real_ocr_test: all checks passed");
    return g_failures == 0 ? 0 : 1;
}
