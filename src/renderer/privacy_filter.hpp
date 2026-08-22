#ifndef WAYBLUR_PRIVACY_FILTER_HPP
#define WAYBLUR_PRIVACY_FILTER_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace wayblur {

enum class ActionType {
    Allow,
    Blur,
    Solid,
    Pixelate
};

struct BoundingBox {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

struct FilterRule {
    std::string app_id;
    ActionType action{ActionType::Allow};
    int radius{30};
    std::string color{"black"};
    int block_size{16};
    BoundingBox box;
};

struct FrameBuffer {
    uint8_t* data{nullptr};
    int width{0};
    int height{0};
    int stride{0};
    std::string format{"RGBA"}; // RGBA, BGRA, BGR, RGB
};

class PrivacyFilter {
public:
    PrivacyFilter() = default;

    void apply_filter(FrameBuffer& frame, const std::vector<FilterRule>& rules);
    void apply_solid(FrameBuffer& frame, const BoundingBox& box, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 255);
    void apply_blur(FrameBuffer& frame, const BoundingBox& box, int radius);
    void apply_pixelate(FrameBuffer& frame, const BoundingBox& box, int block_size);
};

} // namespace wayblur

#endif // WAYBLUR_PRIVACY_FILTER_HPP
