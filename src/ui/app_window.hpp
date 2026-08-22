#ifndef WAYBLUR_APP_WINDOW_HPP
#define WAYBLUR_APP_WINDOW_HPP

#include <adwaita.h>
#include "policy/privacy_policy.hpp"

namespace wayblur {

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    int run(int argc, char* argv[]);

private:
    static void on_activate(GtkApplication* app, gpointer user_data);
    void build_ui(GtkApplication* app);

    void populate_rules_list();
    void add_new_rule(const std::string& target, const std::string& type, ActionType action);
    void remove_rule(const std::string& target);

    PrivacyPolicy policy_;
    GtkWidget* pref_group_{nullptr};
    GtkWidget* target_entry_{nullptr};
    GtkWidget* type_combo_{nullptr};
    GtkWidget* action_combo_{nullptr};
};

} // namespace wayblur

#endif // WAYBLUR_APP_WINDOW_HPP
