#ifndef WAYBLUR_PII_OCR_HPP
#define WAYBLUR_PII_OCR_HPP

#include "pii/pii_classifier.hpp"
#include "renderer/privacy_filter.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wayblur {

// 8-bit grayscale image owned by the caller.
struct GrayImage {
    std::vector<uint8_t> pixels;
    int width{0};
    int height{0};
};

// Converts a 4-bytes-per-pixel frame (RGBA/BGRA/RGBx/BGRx) or 3-byte RGB/BGR to grayscale,
// box-averaging down to at most `max_width` wide. `scale` = output width / input width.
GrayImage to_gray(const FrameBuffer& frame, int max_width, double& scale);

// Thin wrapper over Tesseract. When WayBlur is built without Tesseract,
// available() is false and recognize() returns nothing.
class OcrEngine {
public:
    OcrEngine();
    ~OcrEngine();
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    static bool compiled_in();

    // lang: tesseract language code(s), e.g. "eng" or "eng+hin".
    bool init(const char* lang = "eng");
    bool available() const;
    // Where the loaded model came from: "bundled (<dir>)" or "system". Empty until init().
    const std::string& model_source() const;

    // Returns text lines in the image's own pixel coordinates.
    std::vector<OcrLine> recognize(const GrayImage& img);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wayblur

#endif // WAYBLUR_PII_OCR_HPP
