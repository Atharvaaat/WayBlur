#include "ui/app_window.hpp"
#include <iostream>

namespace wayblur {

struct RemoveContext {
    AppWindow* window;
    std::string target;
};

AppWindow::AppWindow() = default;
AppWindow::~AppWindow() = default;

void AppWindow::populate_rules_list() {
    if (!pref_group_) return;

    // Clear existing children except header
    GtkWidget* child = gtk_widget_get_first_child(pref_group_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        if (ADW_IS_PREFERENCES_ROW(child)) {
            adw_preferences_group_remove(ADW_PREFERENCES_GROUP(pref_group_), child);
        }
        child = next;
    }

    const auto& policies = policy_.policies();
    for (const auto& pol : policies) {
        GtkWidget* row = adw_action_row_new();
        std::string title = pol.app_id + " [" + (pol.type == "dns" ? "DNS/Domain" : "App") + "]";
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title.c_str());

        std::string sub = (pol.action == ActionType::Solid ? "Solid Mask" : (pol.action == ActionType::Blur ? "Blur Mask" : "Pixelate Mask"));
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sub.c_str());

        GtkWidget* sw = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(sw), TRUE);
        gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), sw);

        // Delete button
        GtkWidget* btn_del = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_set_valign(btn_del, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class(btn_del, "flat");

        auto* ctx = new RemoveContext{this, pol.app_id};
        g_signal_connect_data(btn_del, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            auto* c = static_cast<RemoveContext*>(user_data);
            if (c && c->window) {
                c->window->remove_rule(c->target);
            }
            delete c;
        }), ctx, nullptr, G_CONNECT_DEFAULT);

        adw_action_row_add_suffix(ADW_ACTION_ROW(row), btn_del);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(pref_group_), row);
    }
}

void AppWindow::add_new_rule(const std::string& target, const std::string& type, ActionType action) {
    if (target.empty()) return;

    AppPolicy pol;
    pol.app_id = target;
    pol.type = type;
    pol.action = action;
    pol.radius = 30;
    pol.color = "black";
    pol.block_size = 16;

    policy_.add_policy(pol);
    policy_.save_config();
    populate_rules_list();
}

void AppWindow::remove_rule(const std::string& target) {
    if (policy_.remove_policy(target)) {
        policy_.save_config();
        populate_rules_list();
    }
}

void AppWindow::build_ui(GtkApplication* app) {
    policy_.load_config();

    GtkWidget* window = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "WayBlur — Privacy Protection");
    gtk_window_set_default_size(GTK_WINDOW(window), 540, 720);

    GtkWidget* header_bar = adw_header_bar_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), header_bar);

    GtkWidget* clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 500);
    gtk_box_append(GTK_BOX(box), clamp);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    adw_clamp_set_child(ADW_CLAMP(clamp), scroll);

    GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(content_box, 16);
    gtk_widget_set_margin_end(content_box, 16);
    gtk_widget_set_margin_top(content_box, 16);
    gtk_widget_set_margin_bottom(content_box, 16);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content_box);

    // Status Card
    GtkWidget* status_page = adw_status_page_new();
    adw_status_page_set_title(ADW_STATUS_PAGE(status_page), "Screen Sharing Protection");
    adw_status_page_set_description(ADW_STATUS_PAGE(status_page), "Status: ● Protected | Screen sharing: Active");
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(status_page), "security-high-symbolic");
    gtk_box_append(GTK_BOX(content_box), status_page);

    // Add New Target Rule Section
    GtkWidget* add_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(add_group), "Add App or DNS/Domain Target");
    gtk_box_append(GTK_BOX(content_box), add_group);

    target_entry_ = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(target_entry_), "Target Name / DNS Domain");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(target_entry_), "e.g. slack, *.internal.net, keepassxc");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(add_group), target_entry_);

    const char* type_options[] = {"Application (App ID)", "DNS / Domain Name", nullptr};
    type_combo_ = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(type_combo_), "Target Type");
    adw_combo_row_set_model(ADW_COMBO_ROW(type_combo_), G_LIST_MODEL(gtk_string_list_new(type_options)));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(add_group), type_combo_);

    const char* action_options[] = {"Blur Mask", "Solid Mask", "Pixelate Mask", nullptr};
    action_combo_ = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(action_combo_), "Protection Action");
    adw_combo_row_set_model(ADW_COMBO_ROW(action_combo_), G_LIST_MODEL(gtk_string_list_new(action_options)));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(add_group), action_combo_);

    GtkWidget* btn_add = gtk_button_new_with_label("Add Target Rule");
    gtk_widget_add_css_class(btn_add, "suggested-action");
    gtk_widget_add_css_class(btn_add, "pill");
    gtk_widget_set_margin_top(btn_add, 8);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(add_group), btn_add);

    g_signal_connect_data(btn_add, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        auto* self = static_cast<AppWindow*>(user_data);
        if (!self) return;

        const char* text = gtk_editable_get_text(GTK_EDITABLE(self->target_entry_));
        if (!text || std::string(text).empty()) return;

        guint type_idx = adw_combo_row_get_selected(ADW_COMBO_ROW(self->type_combo_));
        guint act_idx = adw_combo_row_get_selected(ADW_COMBO_ROW(self->action_combo_));

        std::string type = (type_idx == 1) ? "dns" : "app";
        ActionType act = (act_idx == 1) ? ActionType::Solid : ((act_idx == 2) ? ActionType::Pixelate : ActionType::Blur);

        self->add_new_rule(std::string(text), type, act);
        gtk_editable_set_text(GTK_EDITABLE(self->target_entry_), "");
    }), this, nullptr, G_CONNECT_DEFAULT);

    // Protected Applications List
    pref_group_ = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(pref_group_), "Active Protection Targets");
    gtk_box_append(GTK_BOX(content_box), pref_group_);

    populate_rules_list();

    // Default Action & Strength Settings
    GtkWidget* settings_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(settings_group), "Global Settings");
    gtk_box_append(GTK_BOX(content_box), settings_group);

    GtkWidget* scale_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(scale_row), "Blur strength");
    GtkWidget* scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 5, 50, 5);
    gtk_range_set_value(GTK_RANGE(scale), 30);
    gtk_widget_set_hexpand(scale, TRUE);
    adw_action_row_add_suffix(ADW_ACTION_ROW(scale_row), scale);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(settings_group), scale_row);

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
