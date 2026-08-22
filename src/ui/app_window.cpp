#include "ui/app_window.hpp"
#include <iostream>

namespace wayblur {

AppWindow::AppWindow() = default;
AppWindow::~AppWindow() = default;

void AppWindow::build_ui(GtkApplication* app) {
    policy_.load_config();

    GtkWidget* window = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "WayBlur — Privacy Protection");
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 680);

    GtkWidget* header_bar = adw_header_bar_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), header_bar);

    GtkWidget* clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 480);
    gtk_box_append(GTK_BOX(box), clamp);

    GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(content_box, 16);
    gtk_widget_set_margin_end(content_box, 16);
    gtk_widget_set_margin_top(content_box, 16);
    gtk_widget_set_margin_bottom(content_box, 16);
    adw_clamp_set_child(ADW_CLAMP(clamp), content_box);

    // Status Card
    GtkWidget* status_page = adw_status_page_new();
    adw_status_page_set_title(ADW_STATUS_PAGE(status_page), "Screen Sharing Protection");
    adw_status_page_set_description(ADW_STATUS_PAGE(status_page), "Status: ● Protected | Screen sharing: Active");
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(status_page), "security-high-symbolic");
    gtk_box_append(GTK_BOX(content_box), status_page);

    // Protected Applications List
    GtkWidget* pref_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(pref_group), "Protected Applications");
    gtk_box_append(GTK_BOX(content_box), pref_group);

    const auto& policies = policy_.policies();
    for (const auto& pol : policies) {
        GtkWidget* row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), pol.app_id.c_str());
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), (pol.action == ActionType::Solid ? "Solid Mask" : "Blur Mask"));
        adw_switch_row_set_active(ADW_SWITCH_ROW(row), TRUE);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(pref_group), row);
    }

    // Default Action & Strength
    GtkWidget* settings_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(settings_group), "Default Protection Settings");
    gtk_box_append(GTK_BOX(content_box), settings_group);

    GtkWidget* scale_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(scale_row), "Blur strength");
    GtkWidget* scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 5, 50, 5);
    gtk_range_set_value(GTK_RANGE(scale), 30);
    gtk_widget_set_hexpand(scale, TRUE);
    adw_action_row_add_suffix(ADW_ACTION_ROW(scale_row), scale);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(settings_group), scale_row);

    // Test Button
    GtkWidget* btn_test = gtk_button_new_with_label("Test Protection");
    gtk_widget_add_css_class(btn_test, "suggested-action");
    gtk_widget_add_css_class(btn_test, "pill");
    gtk_widget_set_margin_top(btn_test, 12);
    gtk_box_append(GTK_BOX(content_box), btn_test);

    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), box);
    gtk_window_present(GTK_WINDOW(window));
}

void AppWindow::on_activate(GtkApplication* app, gpointer user_data) {
    auto* self = static_cast<AppWindow*>(user_data);
    if (self) {
        self->build_ui(app);
    }
}

int AppWindow::run(int argc, char* argv[]) {
    (void)argc;
    AdwApplication* app = adw_application_new("org.wayblur.desktop", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), this);

    char* app_argv[] = { argv[0], nullptr };
    int status = g_application_run(G_APPLICATION(app), 1, app_argv);
    g_object_unref(app);
    return status;
}

} // namespace wayblur
