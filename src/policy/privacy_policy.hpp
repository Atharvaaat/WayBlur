#ifndef WAYBLUR_PRIVACY_POLICY_HPP
#define WAYBLUR_PRIVACY_POLICY_HPP

#include "renderer/privacy_filter.hpp"

#include <string>
#include <vector>

namespace wayblur {

struct AppPolicy {
    std::string app_id;       // Target ID or domain (e.g. "keepassxc", "firefox", "outlook.office.com")
    std::string type{"app"};  // "app" or "dns"
    ActionType action{ActionType::Allow};
    int radius{30};
    std::string color{"black"};
    int block_size{16};
};

class PrivacyPolicy {
public:
    PrivacyPolicy();
    explicit PrivacyPolicy(const std::string& config_path);

    bool load_config(const std::string& path = "");
    bool save_config(const std::string& path = "");

    void add_policy(const AppPolicy& policy);
    bool remove_policy(const std::string& target);
    AppPolicy get_policy_for_app(const std::string& app_id, const std::string& title = "") const;
    ActionType get_default_action() const { return default_action_; }
    void set_default_action(ActionType action) { default_action_ = action; }

    const std::vector<AppPolicy>& policies() const { return policies_; }

    static std::string get_default_config_path();

private:
    std::string config_path_;
    std::vector<AppPolicy> policies_;
    ActionType default_action_{ActionType::Allow};
};

} // namespace wayblur

#endif // WAYBLUR_PRIVACY_POLICY_HPP
