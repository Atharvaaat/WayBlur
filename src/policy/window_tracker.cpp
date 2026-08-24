#include "policy/window_tracker.hpp"

#include <iostream>

namespace wayblur {

WindowTracker::WindowTracker() = default;

bool WindowTracker::init() {
    return mutter_.init();
}

void WindowTracker::inject_synthetic_window(const std::string& app_id, const BoundingBox& box, uint64_t window_id) {
    SurfaceWindowInfo win;
    win.window_id = window_id;
    win.app_id = app_id;
    win.title = app_id;
    win.box = box;
    win.monitor = 0;
    synthetic_windows_.push_back(win);
}

void WindowTracker::clear_synthetic_windows() {
    synthetic_windows_.clear();
}

std::vector<FilterRule> WindowTracker::generate_filter_rules(const PrivacyPolicy& policy) {
    std::vector<FilterRule> rules;

    std::vector<SurfaceWindowInfo> windows = mutter_.fetch_surface_metadata();
    windows.insert(windows.end(), synthetic_windows_.begin(), synthetic_windows_.end());

    for (const auto& win : windows) {
        AppPolicy app_pol = policy.get_policy_for_app(win.app_id, win.title);
        if (app_pol.action != ActionType::Allow) {
            FilterRule rule;
            rule.window_id = win.window_id;
            rule.app_id = win.app_id;
            rule.action = app_pol.action;
            rule.radius = app_pol.radius;
            rule.color = app_pol.color;
            rule.block_size = app_pol.block_size;
            rule.box = win.box;
            rules.push_back(rule);
            std::cout << "[WayBlur:Tracker] Match: '" << win.app_id
                      << "' (title: '" << win.title.substr(0, 40)
                      << "') -> " << (app_pol.action == ActionType::Solid ? "SOLID" :
                                      app_pol.action == ActionType::Blur ? "BLUR" : "PIXELATE")
                      << " at (" << win.box.x << "," << win.box.y
                      << "," << win.box.width << "," << win.box.height << ")\n";
        }
    }

    if (!rules.empty()) {
        std::cout << "[WayBlur:Tracker] Generated " << rules.size()
                  << " filter rule(s) from " << windows.size() << " window(s)\n";
    }

    return rules;
}

} // namespace wayblur
