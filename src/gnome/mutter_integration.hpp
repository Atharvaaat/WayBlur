#ifndef WAYBLUR_MUTTER_INTEGRATION_HPP
#define WAYBLUR_MUTTER_INTEGRATION_HPP

#include "renderer/privacy_filter.hpp"

#include <string>
#include <vector>
#include <gio/gio.h>

namespace wayblur {

struct SurfaceWindowInfo {
    uint64_t window_id{0};
    std::string app_id;
    std::string title;
    BoundingBox box;
    int monitor{0};
};

class MutterIntegration {
public:
    MutterIntegration();
    ~MutterIntegration();

    bool init();
    std::vector<SurfaceWindowInfo> fetch_surface_metadata();
    void send_privacy_masks(const std::vector<FilterRule>& rules);

    bool is_available() const { return available_; }

private:
    GDBusConnection* conn_{nullptr};
    bool available_{false};
};

} // namespace wayblur

#endif // WAYBLUR_MUTTER_INTEGRATION_HPP
