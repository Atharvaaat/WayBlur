#ifndef WAYBLUR_WINDOW_TRACKER_HPP
#define WAYBLUR_WINDOW_TRACKER_HPP

#include "policy/privacy_policy.hpp"
#include "gnome/mutter_integration.hpp"
#include "renderer/privacy_filter.hpp"

#include <vector>
#include <string>

namespace wayblur {

class WindowTracker {
public:
    WindowTracker();

    bool init();
    std::vector<FilterRule> generate_filter_rules(const PrivacyPolicy& policy);

    void inject_synthetic_window(const std::string& app_id, const BoundingBox& box);
    void clear_synthetic_windows();

    MutterIntegration& get_mutter() { return mutter_; }

private:
    MutterIntegration mutter_;
    std::vector<SurfaceWindowInfo> synthetic_windows_;
};

} // namespace wayblur

#endif // WAYBLUR_WINDOW_TRACKER_HPP
