#ifndef WAYBLUR_TEST_MODE_HPP
#define WAYBLUR_TEST_MODE_HPP

#include "policy/privacy_policy.hpp"
#include "renderer/privacy_filter.hpp"

namespace wayblur {

struct TestModeResult {
    bool passed{false};
    uint64_t total_pixels{0};
    uint64_t masked_pixels{0};
    uint64_t untouched_pixels{0};
    std::string summary;
};

class TestMode {
public:
    TestMode();
    TestModeResult run_test();
};

} // namespace wayblur

#endif // WAYBLUR_TEST_MODE_HPP
