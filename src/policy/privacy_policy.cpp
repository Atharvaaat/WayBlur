#include "policy/privacy_policy.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace wayblur {

namespace {

ActionType parse_action_string(const std::string& act) {
    if (act == "blur") return ActionType::Blur;
    if (act == "solid") return ActionType::Solid;
    if (act == "pixelate") return ActionType::Pixelate;
    return ActionType::Allow;
}

std::string action_to_string(ActionType act) {
    switch (act) {
        case ActionType::Blur: return "blur";
        case ActionType::Solid: return "solid";
        case ActionType::Pixelate: return "pixelate";
        case ActionType::Allow:
        default: return "allow";
    }
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string normalize_domain_target(const std::string& input) {
    std::string s = to_lower(input);
    if (s.rfind("https://", 0) == 0) s = s.substr(8);
    else if (s.rfind("http://", 0) == 0) s = s.substr(7);
    if (s.rfind("www.", 0) == 0) s = s.substr(4);
    size_t slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(0, slash);
    if (s.rfind("*.", 0) == 0) s = s.substr(2);
    return s;
}

} // namespace

PrivacyPolicy::PrivacyPolicy() {
    config_path_ = get_default_config_path();
}

PrivacyPolicy::PrivacyPolicy(const std::string& config_path)
    : config_path_(config_path.empty() ? get_default_config_path() : config_path) {}

std::string PrivacyPolicy::get_default_config_path() {
    const char* home = std::getenv("HOME");
    std::string dir = home ? (std::string(home) + "/.config/wayblur") : "./config";
    return dir + "/config.yaml";
}

bool PrivacyPolicy::load_config(const std::string& path) {
    std::string target_path = path.empty() ? config_path_ : path;

    if (!std::filesystem::exists(target_path)) {
        policies_ = {
            {"google-chrome", "app", ActionType::Blur, 30, "black", 16},
            {"firefox", "app", ActionType::Blur, 30, "black", 16},
            {"keepassxc", "app", ActionType::Solid, 30, "black", 16},
            {"obsidian", "app", ActionType::Blur, 25, "black", 16},
            {"*.internal.net", "dns", ActionType::Blur, 30, "black", 16}
        };
        default_action_ = ActionType::Allow;
        return true;
    }

    try {
        YAML::Node config = YAML::LoadFile(target_path);
        policies_.clear();

        if (config["protected_apps"] && config["protected_apps"].IsSequence()) {
            for (const auto& node : config["protected_apps"]) {
                AppPolicy pol;
                pol.type = "app";
                if (node["app_id"]) pol.app_id = node["app_id"].as<std::string>();
                if (node["action"]) pol.action = parse_action_string(node["action"].as<std::string>());
                if (node["radius"]) pol.radius = node["radius"].as<int>();
                if (node["color"]) pol.color = node["color"].as<std::string>();
                if (node["block_size"]) pol.block_size = node["block_size"].as<int>();
                policies_.push_back(pol);
            }
        }

        if (config["protected_domains"] && config["protected_domains"].IsSequence()) {
            for (const auto& node : config["protected_domains"]) {
                AppPolicy pol;
                pol.type = "dns";
                if (node["domain"]) pol.app_id = node["domain"].as<std::string>();
                else if (node["app_id"]) pol.app_id = node["app_id"].as<std::string>();
                if (node["action"]) pol.action = parse_action_string(node["action"].as<std::string>());
                if (node["radius"]) pol.radius = node["radius"].as<int>();
                if (node["color"]) pol.color = node["color"].as<std::string>();
                if (node["block_size"]) pol.block_size = node["block_size"].as<int>();
                policies_.push_back(pol);
            }
        }

        if (config["defaults"] && config["defaults"]["action"]) {
            default_action_ = parse_action_string(config["defaults"]["action"].as<std::string>());
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[PrivacyPolicy] Error loading YAML config: " << e.what() << "\n";
        return false;
    }
}

bool PrivacyPolicy::save_config(const std::string& path) {
    std::string target_path = path.empty() ? config_path_ : path;

    try {
        std::filesystem::create_directories(std::filesystem::path(target_path).parent_path());

        YAML::Node root;
        YAML::Node apps_node;
        YAML::Node dns_node;

        for (const auto& pol : policies_) {
            YAML::Node item;
            item["action"] = action_to_string(pol.action);
            if (pol.action == ActionType::Blur) item["radius"] = pol.radius;
            if (pol.action == ActionType::Solid) item["color"] = pol.color;
            if (pol.action == ActionType::Pixelate) item["block_size"] = pol.block_size;

            if (pol.type == "dns") {
                item["domain"] = pol.app_id;
                dns_node.push_back(item);
            } else {
                item["app_id"] = pol.app_id;
                apps_node.push_back(item);
            }
        }

        root["protected_apps"] = apps_node;
        root["protected_domains"] = dns_node;

        YAML::Node def_node;
        def_node["action"] = action_to_string(default_action_);
        root["defaults"] = def_node;

        std::ofstream fout(target_path);
        fout << root;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[PrivacyPolicy] Error writing config: " << e.what() << "\n";
        return false;
    }
}

void PrivacyPolicy::add_policy(const AppPolicy& policy) {
    for (auto& p : policies_) {
        if (p.app_id == policy.app_id) {
            p = policy;
            return;
        }
    }
    policies_.push_back(policy);
}

bool PrivacyPolicy::remove_policy(const std::string& target) {
    auto it = std::remove_if(policies_.begin(), policies_.end(), [&](const AppPolicy& p) {
        return p.app_id == target;
    });
    if (it != policies_.end()) {
        policies_.erase(it, policies_.end());
        return true;
    }
    return false;
}

AppPolicy PrivacyPolicy::get_policy_for_app(const std::string& app_id, const std::string& title) const {
    std::string norm_app = to_lower(app_id);
    std::string norm_title = to_lower(title);

    for (const auto& p : policies_) {
        std::string norm_target = normalize_domain_target(p.app_id);

        if (norm_target.empty()) continue;

        // 1. Direct app match
        if (norm_app.find(norm_target) != std::string::npos) {
            return p;
        }

        // 2. Title match (domain or window title keyword like "outlook", "office.com", "bank.com")
        if (!norm_title.empty() && norm_title.find(norm_target) != std::string::npos) {
            return p;
        }
    }

    return AppPolicy{app_id, "app", default_action_, 30, "black", 16};
}

} // namespace wayblur
