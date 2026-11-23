// End-to-end check: real Tesseract on a real image, through the classifier.
// Usage: pii_ocr_e2e <image.pgm>   (binary P5 grayscale)
// Prints one line per detected PII region: "<kind> x y w h". Exit 0 if OCR ran.
#include "pii/pii_ocr.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace wayblur;

static bool read_pgm(const char* path, GrayImage& img) {
    std::ifstream f(path, std::ios::binary);
    std::string magic;
    int w, h, maxv;
    if (!(f >> magic >> w >> h >> maxv) || magic != "P5" || maxv != 255) return false;
    f.get();
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * h);
    f.read(reinterpret_cast<char*>(img.pixels.data()), img.pixels.size());
    return static_cast<size_t>(f.gcount()) == img.pixels.size();
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: pii_ocr_e2e image.pgm\n"; return 2; }
    GrayImage img;
    if (!read_pgm(argv[1], img)) { std::cerr << "cannot read PGM\n"; return 2; }

    OcrEngine ocr;
    if (!ocr.init("eng")) return 3;

    PiiClassifier cl;
    for (const auto& line : ocr.recognize(img)) {
        for (const auto& m : cl.scan_line(line)) {
            std::printf("%s %d %d %d %d\n", pii_kind_name(m.kind),
                        m.box.x, m.box.y, m.box.width, m.box.height);
        }
    }
    return 0;
}
