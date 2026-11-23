#ifndef WAYBLUR_PRIVACY_POLICY_HPP
#define WAYBLUR_PRIVACY_POLICY_HPP

#include "pii/pii_classifier.hpp"
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

    // What gets blurred in the shared stream: just the sensitive data (default) or everything.
    BlurMode blur_mode() const { return blur_mode_; }
    void set_blur_mode(BlurMode m) { blur_mode_ = m; }

    // On-screen PII detection settings.
    bool pii_enabled() const { return pii_enabled_; }
    void set_pii_enabled(bool v) { pii_enabled_ = v; }
    uint32_t pii_kinds_mask() const { return pii_kinds_mask_; }
    void set_pii_kinds_mask(uint32_t m) { pii_kinds_mask_ = m; }
    const std::string& ocr_language() const { return ocr_language_; }
    void set_ocr_language(const std::string& l) { ocr_language_ = l; }

    static std::string get_default_config_path();

private:
    std::string config_path_;
    std::vector<AppPolicy> policies_;
    ActionType default_action_{ActionType::Allow};
    BlurMode blur_mode_{BlurMode::DataOnly};
    bool pii_enabled_{true};
    uint32_t pii_kinds_mask_{kAllPiiKinds};
    std::string ocr_language_{"eng"};
};

} // namespace wayblur

#endif // WAYBLUR_PRIVACY_POLICY_HPP
