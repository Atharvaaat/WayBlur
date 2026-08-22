#ifndef WAYBLUR_PRIVACY_OVERLAY_HPP
#define WAYBLUR_PRIVACY_OVERLAY_HPP

#include <gtk/gtk.h>
#include "renderer/privacy_filter.hpp"

#include <vector>

namespace wayblur {

class PrivacyOverlay {
public:
    PrivacyOverlay();
    ~PrivacyOverlay();

    void show();
    void hide();
    void set_filter_rules(const std::vector<FilterRule>& rules);

private:
    GtkWidget* window_{nullptr};
    GtkWidget* drawing_area_{nullptr};
    std::vector<FilterRule> filter_rules_;

    static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data);
};

} // namespace wayblur

#endif // WAYBLUR_PRIVACY_OVERLAY_HPP
