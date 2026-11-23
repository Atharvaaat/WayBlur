#ifndef WAYBLUR_DOCTOR_HPP
#define WAYBLUR_DOCTOR_HPP

#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

namespace wayblur {

struct DoctorReport {
    std::string os_version{"Unknown"};
    std::string desktop{"Unknown"};
    std::string session_type{"Unknown"};
    std::string mutter_version{"Unknown"};
    std::string portal_version{"Unknown"};
    std::string portal_backend{"Unknown"};
    uint32_t screencast_version{0};
    uint32_t screencast_source_types{0}; // 1: Monitor, 2: Window, 4: Virtual
    uint32_t screencast_cursor_modes{0}; // 1: Hidden, 2: Embedded, 4: Metadata
    std::string pipewire_version{"Unknown"};
    std::string wireplumber_version{"Unknown"};
    std::string gpu_info{"Unknown"};
    bool egl_available{false};
    std::string egl_details{"Unavailable"};
    bool vulkan_available{false};
    std::string vulkan_details{"Unavailable"};
    std::string gstreamer_version{"Unknown"};
    std::string ocr_status{"Unknown"};   // sensitive-data detection engine

    bool is_wayland_gnome() const {
        return session_type == "Wayland" || session_type == "wayland";
    }
};

DoctorReport run_diagnostics();
void print_doctor_report(const DoctorReport& report, std::ostream& os = std::cout);

} // namespace wayblur

#endif // WAYBLUR_DOCTOR_HPP
