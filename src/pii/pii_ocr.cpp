#include "pii/pii_ocr.hpp"

#include <iostream>

#ifdef WAYBLUR_HAVE_TESSERACT
#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace wayblur {

GrayImage to_gray(const FrameBuffer& frame, int max_width, double& scale) {
    GrayImage out;
    scale = 1.0;
    if (!frame.data || frame.width <= 0 || frame.height <= 0) return out;

    const bool three = (frame.format == "RGB" || frame.format == "BGR");
    const int bpp = three ? 3 : 4;
    const bool bgr_order = (frame.format == "BGRA" || frame.format == "BGRx" ||
                            frame.format == "BGR");
    const int ri = bgr_order ? 2 : 0;
    const int bi = bgr_order ? 0 : 2;

    int step = 1;
    if (max_width > 0 && frame.width > max_width) {
        step = static_cast<int>(std::ceil(static_cast<double>(frame.width) / max_width));
    }
    out.width = frame.width / step;
    out.height = frame.height / step;
    scale = 1.0 / step;
    out.pixels.resize(static_cast<size_t>(out.width) * out.height);

    for (int y = 0; y < out.height; ++y) {
        const uint8_t* row = frame.data + static_cast<size_t>(y) * step * frame.stride;
        uint8_t* dst = out.pixels.data() + static_cast<size_t>(y) * out.width;
        for (int x = 0; x < out.width; ++x) {
            // Box-average each step×step block so thin glyph strokes survive downscaling.
            int sum = 0;
            for (int dy = 0; dy < step; ++dy) {
                const uint8_t* r = row + static_cast<size_t>(dy) * frame.stride + x * step * bpp;
                for (int dx = 0; dx < step; ++dx) {
                    const uint8_t* p = r + dx * bpp;
                    sum += (p[ri] * 77 + p[1] * 150 + p[bi] * 29) >> 8;
                }
            }
            dst[x] = static_cast<uint8_t>(sum / (step * step));
        }
    }
    return out;
}

#ifdef WAYBLUR_HAVE_TESSERACT

namespace {

// Folder holding the model that ships inside the app package. WAYBLUR_TESSDATA overrides it
// (useful for running from a build directory).
std::string bundled_tessdata_dir() {
    if (const char* env = std::getenv("WAYBLUR_TESSDATA")) return env;
#ifdef WAYBLUR_TESSDATA_DIR
    return WAYBLUR_TESSDATA_DIR;
#else
    return "";
#endif
}

// True when every language in "eng+hin" has a model file in `dir`.
bool has_all_languages(const std::string& dir, const std::string& lang) {
    if (dir.empty()) return false;
    size_t start = 0;
    while (start <= lang.size()) {
        size_t plus = lang.find('+', start);
        std::string tok = lang.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        if (tok.empty() || !std::filesystem::exists(dir + "/" + tok + ".traineddata")) return false;
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    return true;
}

} // namespace

struct OcrEngine::Impl {
    tesseract::TessBaseAPI api;
    bool ready{false};
    std::string source;
};

OcrEngine::OcrEngine() : impl_(std::make_unique<Impl>()) {}

OcrEngine::~OcrEngine() {
    if (impl_ && impl_->ready) impl_->api.End();
}

bool OcrEngine::compiled_in() { return true; }

bool OcrEngine::init(const char* lang) {
    if (impl_->ready) return true;
    // Prefer the model shipped with the app so nothing else needs to be installed; other
    // languages fall back to the system's Tesseract data.
    const std::string bundled = bundled_tessdata_dir();
    const bool use_bundled = has_all_languages(bundled, lang);
    impl_->source = use_bundled ? "bundled (" + bundled + ")" : "system";

    // LSTM-only: faster and more accurate on screen text than the legacy engine.
    if (impl_->api.Init(use_bundled ? bundled.c_str() : nullptr, lang, tesseract::OEM_LSTM_ONLY) != 0) {
        std::cerr << "[WayBlur:OCR] Could not load Tesseract language data '" << lang
                  << "' (install tesseract-ocr-" << lang << ")\n";
        return false;
    }
    // Screens are scattered UI text, not page layouts.
    impl_->api.SetPageSegMode(tesseract::PSM_SPARSE_TEXT);
    impl_->ready = true;
    return true;
}

bool OcrEngine::available() const { return impl_ && impl_->ready; }

const std::string& OcrEngine::model_source() const {
    static const std::string empty;
    return impl_ ? impl_->source : empty;
}

std::vector<OcrLine> OcrEngine::recognize(const GrayImage& img) {
    std::vector<OcrLine> lines;
    if (!available() || img.width <= 0 || img.height <= 0 ||
        img.pixels.size() < static_cast<size_t>(img.width) * img.height) {
        return lines;
    }

    // Tesseract expects dark text on a light background. Dark-mode UIs are the opposite, and
    // reading them as-is costs a lot of accuracy, so invert when the crop is mostly dark.
    const uint8_t* pixels = img.pixels.data();
    std::vector<uint8_t> inverted;
    {
        uint64_t sum = 0;
        const size_t n = static_cast<size_t>(img.width) * img.height;
        const size_t step = n > 4096 ? n / 4096 : 1;
        size_t cnt = 0;
        for (size_t i = 0; i < n; i += step, ++cnt) sum += img.pixels[i];
        if (cnt && sum / cnt < 110) {
            inverted.resize(n);
            for (size_t i = 0; i < n; ++i) inverted[i] = static_cast<uint8_t>(255 - img.pixels[i]);
            pixels = inverted.data();
        }
    }
    impl_->api.SetImage(pixels, img.width, img.height, 1, img.width);
    impl_->api.SetSourceResolution(150);
    if (impl_->api.Recognize(nullptr) != 0) return lines;

    std::unique_ptr<tesseract::ResultIterator> it(impl_->api.GetIterator());
    if (!it) return lines;

    constexpr tesseract::PageIteratorLevel kWord = tesseract::RIL_WORD;
    constexpr tesseract::PageIteratorLevel kLine = tesseract::RIL_TEXTLINE;
    constexpr float kMinConfidence = 30.0f;

    do {
        if (it->IsAtBeginningOf(kLine)) lines.emplace_back();

        std::unique_ptr<char[]> text(it->GetUTF8Text(kWord));
        if (!text || lines.empty()) continue;
        if (it->Confidence(kWord) < kMinConfidence) continue;

        int l, t, r, b;
        if (!it->BoundingBox(kWord, &l, &t, &r, &b)) continue;

        std::string word(text.get());
        while (!word.empty() && std::isspace(static_cast<unsigned char>(word.back()))) word.pop_back();
        if (word.empty()) continue;

        lines.back().words.push_back({word, {l, t, r - l, b - t}});
    } while (it->Next(kWord));

    return lines;
}

#else // !WAYBLUR_HAVE_TESSERACT

struct OcrEngine::Impl {};

OcrEngine::OcrEngine() : impl_(std::make_unique<Impl>()) {}
OcrEngine::~OcrEngine() = default;
bool OcrEngine::compiled_in() { return false; }
bool OcrEngine::init(const char*) {
    std::cerr << "[WayBlur:OCR] Built without Tesseract; PII detection is disabled. "
                 "Install libtesseract-dev and rebuild.\n";
    return false;
}
bool OcrEngine::available() const { return false; }
const std::string& OcrEngine::model_source() const {
    static const std::string none = "not built with OCR";
    return none;
}
std::vector<OcrLine> OcrEngine::recognize(const GrayImage&) { return {}; }

#endif

} // namespace wayblur
