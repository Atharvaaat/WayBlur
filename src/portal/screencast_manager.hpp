#ifndef WAYBLUR_SCREENCAST_MANAGER_HPP
#define WAYBLUR_SCREENCAST_MANAGER_HPP

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <gio/gio.h>

namespace wayblur {

enum class SourceType : uint32_t {
    Monitor = 1,
    Window = 2,
    Virtual = 4
};

enum class CursorMode : uint32_t {
    Hidden = 1,
    Embedded = 2,
    Metadata = 4
};

struct ScreenCastStream {
    uint32_t node_id{0};
    uint64_t pipewire_serial{0};
    int width{0};
    int height{0};
    std::string source_type;
};

class ScreenCastManager {
public:
    ScreenCastManager();
    ~ScreenCastManager();

    bool init_dbus();
    bool create_session();
    bool select_sources(SourceType source_type = SourceType::Monitor, CursorMode cursor_mode = CursorMode::Embedded, bool multiple = false);
    bool start_session();
    int open_pipewire_remote();

    void set_session_closed_callback(std::function<void()> cb);

    bool is_active() const { return active_; }
    const std::string& session_handle() const { return session_handle_; }
    const std::vector<ScreenCastStream>& streams() const { return streams_; }

private:
    GDBusConnection* conn_{nullptr};
    std::string session_handle_;
    std::string request_handle_;
    bool active_{false};
    int pipewire_fd_{-1};
    std::vector<ScreenCastStream> streams_;
    std::function<void()> on_closed_cb_;
    guint signal_id_{0};

    // Portal methods answer asynchronously on org.freedesktop.portal.Request::Response.
    // Calls `do_call`, then pumps the main context until the response for `token`
    // arrives. On success returns true with the response `results` (a{sv}, caller unrefs).
    bool call_and_wait(const std::string& token,
                       const std::function<bool()>& do_call,
                       int timeout_ms,
                       GVariant** results);
    std::string request_path_for(const std::string& token) const;
    std::string sender_token() const;

    static void on_signal_closed(GDBusConnection* connection,
                                 const gchar* sender_name,
                                 const gchar* object_path,
                                 const gchar* interface_name,
                                 const gchar* signal_name,
                                 GVariant* parameters,
                                 gpointer user_data);
};

} // namespace wayblur

#endif // WAYBLUR_SCREENCAST_MANAGER_HPP
