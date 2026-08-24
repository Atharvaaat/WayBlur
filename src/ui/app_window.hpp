#ifndef WAYBLUR_APP_WINDOW_HPP
#define WAYBLUR_APP_WINDOW_HPP

#include <adwaita.h>
#include "policy/privacy_policy.hpp"
#include "security/share_session_monitor.hpp"
#include "pipewire/capture_pipeline.hpp"
#include "policy/window_tracker.hpp"

#include "ui/privacy_overlay.hpp"

namespace wayblur {

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    int run(int argc, char* argv[]);

    void start_protection();
    void stop_protection();
    void update_protection_rules();

private:
    static void on_activate(GtkApplication* app, gpointer user_data);
    void build_ui(GtkApplication* app);

    void populate_rules_list();
    void add_new_rule(const std::string& target, const std::string& type, ActionType action);
    void remove_rule(const std::string& target);

    void run_doctor_dialog(GtkWindow* parent);
    void run_testmode_dialog(GtkWindow* parent);
    void run_benchmark_dialog(GtkWindow* parent);

    PrivacyPolicy policy_;
    ShareSessionMonitor session_monitor_;
    CapturePipeline pipeline_;
    WindowTracker window_tracker_;
    ScreenCastManager portal_mgr_;

    GtkWidget* status_page_{nullptr};
    GtkWidget* status_switch_{nullptr};
    GtkWidget* pref_group_{nullptr};
    GtkWidget* target_entry_{nullptr};
    GtkWidget* type_combo_{nullptr};
    GtkWidget* action_combo_{nullptr};

    bool is_protecting_{false};
    bool pipeline_active_{false};
    guint timer_id_{0};
};

} // namespace wayblur

#endif // WAYBLUR_APP_WINDOW_HPP
