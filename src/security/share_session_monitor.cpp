#include "security/share_session_monitor.hpp"

namespace wayblur {

ShareSessionMonitor::ShareSessionMonitor() = default;

ShareSessionMonitor::~ShareSessionMonitor() {
    stop_monitoring();
}

bool ShareSessionMonitor::start_monitoring() {
    if (!portal_mgr_.init_dbus()) {
        fail_closed_ = true; // Security fail closed if D-Bus inaccessible
        return false;
    }

    portal_mgr_.set_session_closed_callback([this]() {
        if (state_cb_) {
            state_cb_(false);
        }
    });

    monitoring_ = true;
    return true;
}

void ShareSessionMonitor::stop_monitoring() {
    monitoring_ = false;
}

bool ShareSessionMonitor::is_screencast_active() const {
    return portal_mgr_.is_active();
}

void ShareSessionMonitor::set_session_state_callback(std::function<void(bool active)> cb) {
    state_cb_ = cb;
}

} // namespace wayblur
