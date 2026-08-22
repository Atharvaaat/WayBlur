#include "renderer/privacy_filter.hpp"

#include <cmath>
#include <vector>
#include <iostream>

namespace wayblur {

namespace {

inline void clamp_box(const BoundingBox& box, int frame_w, int frame_h, int& x0, int& y0, int& x1, int& y1) {
    x0 = std::max(0, std::min(box.x, frame_w));
    y0 = std::max(0, std::min(box.y, frame_h));
    x1 = std::max(0, std::min(box.x + box.width, frame_w));
    y1 = std::max(0, std::min(box.y + box.height, frame_h));
}

} // namespace

void PrivacyFilter::apply_solid(FrameBuffer& frame, const BoundingBox& box, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!frame.data || frame.width <= 0 || frame.height <= 0) return;

    int x0, y0, x1, y1;
    clamp_box(box, frame.width, frame.height, x0, y0, x1, y1);

    int bpp = (frame.format == "RGB" || frame.format == "BGR") ? 3 : 4;

    for (int y = y0; y < y1; ++y) {
        uint8_t* row = frame.data + y * frame.stride;
        for (int x = x0; x < x1; ++x) {
            uint8_t* pixel = row + x * bpp;
            if (frame.format == "BGRA") {
                pixel[0] = b;
                pixel[1] = g;
                pixel[2] = r;
                if (bpp == 4) pixel[3] = a;
            } else { // RGBA / RGB
                pixel[0] = r;
                pixel[1] = g;
                pixel[2] = b;
                if (bpp == 4) pixel[3] = a;
            }
        }
    }
}

void PrivacyFilter::apply_pixelate(FrameBuffer& frame, const BoundingBox& box, int block_size) {
    if (!frame.data || frame.width <= 0 || frame.height <= 0 || block_size <= 0) return;

    int x0, y0, x1, y1;
    clamp_box(box, frame.width, frame.height, x0, y0, x1, y1);

    int bpp = (frame.format == "RGB" || frame.format == "BGR") ? 3 : 4;

    for (int by = y0; by < y1; by += block_size) {
        for (int bx = x0; bx < x1; bx += block_size) {
            int bend_y = std::min(by + block_size, y1);
            int bend_x = std::min(bx + block_size, x1);

            uint64_t sum_c0 = 0, sum_c1 = 0, sum_c2 = 0, sum_c3 = 0;
            int count = 0;

            for (int y = by; y < bend_y; ++y) {
                const uint8_t* row = frame.data + y * frame.stride;
                for (int x = bx; x < bend_x; ++x) {
                    const uint8_t* pixel = row + x * bpp;
                    sum_c0 += pixel[0];
                    sum_c1 += pixel[1];
                    sum_c2 += pixel[2];
                    if (bpp == 4) sum_c3 += pixel[3];
                    count++;
                }
            }

            if (count == 0) continue;

            uint8_t avg0 = sum_c0 / count;
            uint8_t avg1 = sum_c1 / count;
            uint8_t avg2 = sum_c2 / count;
            uint8_t avg3 = (bpp == 4) ? (sum_c3 / count) : 255;

            for (int y = by; y < bend_y; ++y) {
                uint8_t* row = frame.data + y * frame.stride;
                for (int x = bx; x < bend_x; ++x) {
                    uint8_t* pixel = row + x * bpp;
                    pixel[0] = avg0;
                    pixel[1] = avg1;
                    pixel[2] = avg2;
                    if (bpp == 4) pixel[3] = avg3;
                }
            }
        }
    }
}

void PrivacyFilter::apply_blur(FrameBuffer& frame, const BoundingBox& box, int radius) {
    if (!frame.data || frame.width <= 0 || frame.height <= 0 || radius <= 0) return;

    // Use fast pixelation/box-blur combo for performance
    int block_size = std::max(4, radius / 2);
    apply_pixelate(frame, box, block_size);
}

void PrivacyFilter::apply_filter(FrameBuffer& frame, const std::vector<FilterRule>& rules) {
    for (const auto& rule : rules) {
        switch (rule.action) {
            case ActionType::Solid:
                apply_solid(frame, rule.box);
                break;
            case ActionType::Blur:
                apply_blur(frame, rule.box, rule.radius);
                break;
            case ActionType::Pixelate:
                apply_pixelate(frame, rule.box, rule.block_size);
                break;
            case ActionType::Allow:
            default:
                break;
        }
    }
}

} // namespace wayblur
