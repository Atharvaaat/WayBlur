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

    PrivacyPolicy policy_;
};

} // namespace wayblur

#endif // WAYBLUR_APP_WINDOW_HPP
