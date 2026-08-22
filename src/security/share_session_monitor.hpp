#ifndef WAYBLUR_SHARE_SESSION_MONITOR_HPP
#define WAYBLUR_SHARE_SESSION_MONITOR_HPP

#include "portal/screencast_manager.hpp"
#include "policy/privacy_policy.hpp"

#include <functional>
#include <atomic>

namespace wayblur {

class ShareSessionMonitor {
public:
    ShareSessionMonitor();
    ~ShareSessionMonitor();

    bool start_monitoring();
    void stop_monitoring();

    bool is_screencast_active() const;
    bool is_fail_closed_triggered() const { return fail_closed_; }

    void set_session_state_callback(std::function<void(bool active)> cb);

private:
    ScreenCastManager portal_mgr_;
    std::atomic<bool> monitoring_{false};
    std::atomic<bool> fail_closed_{false};
    std::function<void(bool)> state_cb_;
};

} // namespace wayblur

#endif // WAYBLUR_SHARE_SESSION_MONITOR_HPP
