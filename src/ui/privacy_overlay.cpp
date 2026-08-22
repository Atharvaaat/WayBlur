#include "ui/privacy_overlay.hpp"

#include <iostream>

namespace wayblur {

PrivacyOverlay::PrivacyOverlay() = default;

PrivacyOverlay::~PrivacyOverlay() {
    hide();
}

void PrivacyOverlay::on_draw(GtkDrawingArea*, cairo_t* cr, int, int, gpointer user_data) {
    auto* self = static_cast<PrivacyOverlay*>(user_data);
    if (!self) return;

    // Clear transparent background
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    for (const auto& rule : self->filter_rules_) {
        if (rule.action == ActionType::Allow) continue;

        double x = rule.box.x;
        double y = rule.box.y;
        double w = rule.box.width;
        double h = rule.box.height;

        if (w <= 0 || h <= 0) continue;

        // Draw rounded rectangle mask over protected window region
        double corner_radius = 8.0;
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + w - corner_radius, y + corner_radius, corner_radius, -M_PI_2, 0);
        cairo_arc(cr, x + w - corner_radius, y + h - corner_radius, corner_radius, 0, M_PI_2);
        cairo_arc(cr, x + corner_radius, y + h - corner_radius, corner_radius, M_PI_2, M_PI);
        cairo_arc(cr, x + corner_radius, y + corner_radius, corner_radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cr);

        if (rule.action == ActionType::Solid) {
            // Dark solid mask
            cairo_set_source_rgba(cr, 0.05, 0.05, 0.08, 0.98);
        } else {
            // Frosted glass privacy mask
            cairo_set_source_rgba(cr, 0.12, 0.14, 0.20, 0.94);
        }
        cairo_fill_preserve(cr);

        // Border outline
        cairo_set_source_rgba(cr, 0.35, 0.45, 0.70, 0.80);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);

        // Render Privacy Badge Text
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 14.0);
        cairo_set_source_rgba(cr, 0.90, 0.95, 1.0, 0.95);

        cairo_move_to(cr, x + 16, y + 28);
        std::string label = "WAYBLUR PRIVACY PROTECTED (" + (rule.action == ActionType::Solid ? std::string("SOLID") : std::string("BLUR")) + ")";
        cairo_show_text(cr, label.c_str());
    }
}

void PrivacyOverlay::show() {
    if (window_) return;

    window_ = gtk_window_new();
    gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);

    // Make window background transparent
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, "window { background: rgba(0,0,0,0); }");
    GtkStyleContext* ctx = gtk_widget_get_style_context(window_);
    gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_window_fullscreen(GTK_WINDOW(window_));
    gtk_window_maximize(GTK_WINDOW(window_));

    drawing_area_ = gtk_drawing_area_new();
    gtk_widget_set_hexpand(drawing_area_, TRUE);
    gtk_widget_set_vexpand(drawing_area_, TRUE);

    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area_), on_draw, this, nullptr);
    gtk_window_set_child(GTK_WINDOW(window_), drawing_area_);

    gtk_widget_set_visible(window_, TRUE);
}

void PrivacyOverlay::hide() {
    if (window_) {
        gtk_window_destroy(GTK_WINDOW(window_));
        window_ = nullptr;
        drawing_area_ = nullptr;
    }
}

void PrivacyOverlay::set_filter_rules(const std::vector<FilterRule>& rules) {
    filter_rules_ = rules;
    if (drawing_area_) {
        gtk_widget_queue_draw(drawing_area_);
    }
}

} // namespace wayblur
