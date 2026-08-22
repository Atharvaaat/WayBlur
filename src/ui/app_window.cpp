#include "ui/app_window.hpp"
#include "core/doctor.hpp"
#include "cli/test_mode.hpp"
#include "cli/benchmark.hpp"

#include <iostream>
#include <sstream>

namespace wayblur {

struct RemoveContext {
    AppWindow* window;
    std::string target;
};

struct DialogContext {
    AppWindow* window;
    GtkWindow* parent;
};

AppWindow::AppWindow() = default;
AppWindow::~AppWindow() {
    stop_protection();
}

void AppWindow::update_protection_rules() {
    if (!is_protecting_) return;

    auto rules = window_tracker_.generate_filter_rules(policy_);
    pipeline_.set_filter_rules(rules);
}

void AppWindow::start_protection() {
    if (is_protecting_) return;

    is_protecting_ = true;
    policy_.load_config();
    window_tracker_.init();
    session_monitor_.start_monitoring();

    pipeline_.init();
    pipeline_.start();

    // Initial rule generation
    update_protection_rules();

    // Schedule 100ms periodic rule updates
    timer_id_ = g_timeout_add(100, [](gpointer user_data) -> gboolean {
        auto* self = static_cast<AppWindow*>(user_data);
        if (self && self->is_protecting_) {
            self->update_protection_rules();
            return G_SOURCE_CONTINUE;
        }
        return G_SOURCE_REMOVE;
    }, this);

    if (status_page_) {
        adw_status_page_set_title(ADW_STATUS_PAGE(status_page_), "Privacy Protection Active");
        adw_status_page_set_description(ADW_STATUS_PAGE(status_page_), "Status: ● PROTECTING | Screen sharing streams masked");
        adw_status_page_set_icon_name(ADW_STATUS_PAGE(status_page_), "security-high-symbolic");
    }
}

void AppWindow::stop_protection() {
    if (!is_protecting_) return;

    is_protecting_ = false;
    if (timer_id_ > 0) {
        g_source_remove(timer_id_);
        timer_id_ = 0;
    }

    pipeline_.stop();
    session_monitor_.stop_monitoring();

    if (status_page_) {
        adw_status_page_set_title(ADW_STATUS_PAGE(status_page_), "Protection Inactive");
        adw_status_page_set_description(ADW_STATUS_PAGE(status_page_), "Status: ○ IDLE | Toggle switch to start protection");
        adw_status_page_set_icon_name(ADW_STATUS_PAGE(status_page_), "security-medium-symbolic");
    }
}

void AppWindow::populate_rules_list() {
    if (!pref_group_) return;

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

        GtkWidget* btn_del = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_set_valign(btn_del, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class(btn_del, "flat");

        auto* ctx = new RemoveContext{this, pol.app_id};
        g_signal_connect_data(btn_del, "clicked", G_CALLBACK((GCallback)+[](GtkButton*, gpointer user_data) {
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
    update_protection_rules();
}

void AppWindow::remove_rule(const std::string& target) {
    if (policy_.remove_policy(target)) {
        policy_.save_config();
        populate_rules_list();
        update_protection_rules();
    }
}

void AppWindow::run_doctor_dialog(GtkWindow* parent) {
    DoctorReport report = run_diagnostics();
    std::ostringstream ss;
    ss << "OS: " << report.os_version << "\n"
       << "Desktop: " << report.desktop << "\n"
       << "Session: " << report.session_type << "\n"
       << "Mutter: " << report.mutter_version << "\n"
       << "Portal: " << report.portal_version << "\n"
       << "PipeWire: " << report.pipewire_version << "\n"
       << "WirePlumber: " << report.wireplumber_version << "\n"
       << "GPU: " << report.gpu_info << "\n"
       << "EGL: " << report.egl_details << "\n"
       << "GStreamer: " << report.gstreamer_version;

    GtkWidget* dialog = adw_message_dialog_new(parent, "WayBlur Doctor Report", ss.str().c_str());
    adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dialog), "ok", "Close");
    gtk_window_present(GTK_WINDOW(dialog));
}

void AppWindow::run_testmode_dialog(GtkWindow* parent) {
    TestMode test_mode;
    TestModeResult res = test_mode.run_test();

    GtkWidget* dialog = adw_message_dialog_new(parent, res.passed ? "Test Mode: PASSED" : "Test Mode: FAILED", res.summary.c_str());
    adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dialog), "ok", "Close");
    gtk_window_present(GTK_WINDOW(dialog));
}

void AppWindow::run_benchmark_dialog(GtkWindow* parent) {
    Benchmark bm;
    auto reports = bm.run_benchmark();
    std::ostringstream ss;
    for (const auto& r : reports) {
        ss << r.resolution << " @ " << r.fps << " FPS:\n"
           << "  Capture Latency: " << r.capture_latency_ms << " ms\n"
           << "  Process Latency: " << r.processing_latency_ms << " ms\n"
           << "  Dropped Frames: " << r.dropped_frames << "\n"
           << "  CPU Usage: " << r.cpu_utilization << "\n"
           << "  GPU Usage: " << r.gpu_utilization << "\n\n";
    }

    GtkWidget* dialog = adw_message_dialog_new(parent, "Performance Benchmark Results", ss.str().c_str());
    adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dialog), "ok", "Close");
    gtk_window_present(GTK_WINDOW(dialog));
}

void AppWindow::build_ui(GtkApplication* app) {
    policy_.load_config();

    GtkWidget* window = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "WayBlur — Privacy Protection");
    gtk_window_set_default_size(GTK_WINDOW(window), 560, 760);

    GtkWidget* header_bar = adw_header_bar_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), header_bar);

    GtkWidget* clamp = adw_clamp_new();
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), main_box);

    // Fixed Top Status Area
    status_page_ = adw_status_page_new();
    adw_status_page_set_title(ADW_STATUS_PAGE(status_page_), "Privacy Protection Inactive");
    adw_status_page_set_description(ADW_STATUS_PAGE(status_page_), "Status: ○ STANDBY | Safe to configure rules");
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(status_page_), "security-low-symbolic");
    gtk_widget_set_margin_top(status_page_, 12);
    gtk_widget_set_margin_bottom(status_page_, 12);
    gtk_box_append(GTK_BOX(main_box), status_page_);

    // Enable Extension Button
    GtkWidget* ext_btn = gtk_button_new_with_label("Install & Enable GNOME Extension");
    gtk_widget_set_margin_start(ext_btn, 32);
    gtk_widget_set_margin_end(ext_btn, 32);
    gtk_widget_set_margin_bottom(ext_btn, 12);
    g_signal_connect(ext_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        system("gnome-extensions enable wayblur@org.gnome.Shell.WayBlur || true");
    }), this);
    gtk_box_append(GTK_BOX(main_box), ext_btn);

    // Scrollable Configuration Area
    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_append(GTK_BOX(main_box), scrolled);

    GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(content_box, 24);
    gtk_widget_set_margin_end(content_box, 24);
    gtk_widget_set_margin_top(content_box, 12);
    gtk_widget_set_margin_bottom(content_box, 24);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), content_box);

    GtkWidget* toggle_group = adw_preferences_group_new();
    gtk_box_append(GTK_BOX(content_box), toggle_group);

    GtkWidget* toggle_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(toggle_row), "Active Screen-Share Protection");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(toggle_row), "Automatically mask protected windows &amp; DNS domains");

    status_switch_ = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(status_switch_), FALSE);
    gtk_widget_set_valign(status_switch_, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(toggle_row), status_switch_);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(toggle_group), toggle_row);

    g_signal_connect_data(status_switch_, "notify::active", G_CALLBACK((GCallback)+[](GObject* sw, GParamSpec*, gpointer user_data) {
        auto* self = static_cast<AppWindow*>(user_data);
        if (!self) return;
        gboolean active = gtk_switch_get_active(GTK_SWITCH(sw));
        if (active) {
            self->start_protection();
        } else {
            self->stop_protection();
        }
    }), this, nullptr, G_CONNECT_DEFAULT);

    // Add New Target Rule Section
    GtkWidget* add_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(add_group), "Add App or DNS/Domain Target");
    gtk_box_append(GTK_BOX(content_box), add_group);

    target_entry_ = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(target_entry_), "Target Name / DNS Domain (e.g. outlook.office.com, slack)");
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

    g_signal_connect_data(btn_add, "clicked", G_CALLBACK((GCallback)+[](GtkButton*, gpointer user_data) {
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

    // Active Protection Targets List
    pref_group_ = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(pref_group_), "Active Protection Targets");
    gtk_box_append(GTK_BOX(content_box), pref_group_);

    populate_rules_list();

    // Tools & Diagnostics Group (Doctor, Test, Benchmark parity)
    GtkWidget* tools_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(tools_group), "Diagnostics and Performance Profiling");
    gtk_box_append(GTK_BOX(content_box), tools_group);

    GtkWidget* tools_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(tools_box, 4);

    GtkWidget* btn_doc = gtk_button_new_with_label("Run Doctor Check");
    gtk_widget_set_hexpand(btn_doc, TRUE);
    auto* doc_ctx = new DialogContext{this, GTK_WINDOW(window)};
    g_signal_connect_data(btn_doc, "clicked", G_CALLBACK((GCallback)+[](GtkButton*, gpointer user_data) {
        auto* c = static_cast<DialogContext*>(user_data);
        if (c && c->window) c->window->run_doctor_dialog(c->parent);
    }), doc_ctx, nullptr, G_CONNECT_DEFAULT);

    GtkWidget* btn_test = gtk_button_new_with_label("Run Test Mode");
    gtk_widget_set_hexpand(btn_test, TRUE);
    auto* test_ctx = new DialogContext{this, GTK_WINDOW(window)};
    g_signal_connect_data(btn_test, "clicked", G_CALLBACK((GCallback)+[](GtkButton*, gpointer user_data) {
        auto* c = static_cast<DialogContext*>(user_data);
        if (c && c->window) c->window->run_testmode_dialog(c->parent);
    }), test_ctx, nullptr, G_CONNECT_DEFAULT);

    GtkWidget* btn_bench = gtk_button_new_with_label("Run Benchmark");
    gtk_widget_set_hexpand(btn_bench, TRUE);
    auto* bench_ctx = new DialogContext{this, GTK_WINDOW(window)};
    g_signal_connect_data(btn_bench, "clicked", G_CALLBACK((GCallback)+[](GtkButton*, gpointer user_data) {
        auto* c = static_cast<DialogContext*>(user_data);
        if (c && c->window) c->window->run_benchmark_dialog(c->parent);
    }), bench_ctx, nullptr, G_CONNECT_DEFAULT);

    gtk_box_append(GTK_BOX(tools_box), btn_doc);
    gtk_box_append(GTK_BOX(tools_box), btn_test);
    gtk_box_append(GTK_BOX(tools_box), btn_bench);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(tools_group), tools_box);

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
