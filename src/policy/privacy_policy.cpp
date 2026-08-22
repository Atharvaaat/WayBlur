#include "policy/privacy_policy.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <iostream>
#include <cstdlib>
#include <filesystem>

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
        // Populate default rules matching prompt requirements
        policies_ = {
            {"google-chrome", ActionType::Blur, 30, "black", 16},
            {"firefox", ActionType::Blur, 30, "black", 16},
            {"keepassxc", ActionType::Solid, 30, "black", 16},
            {"obsidian", ActionType::Blur, 25, "black", 16}
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
                if (node["app_id"]) pol.app_id = node["app_id"].as<std::string>();
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

        for (const auto& pol : policies_) {
            YAML::Node item;
            item["app_id"] = pol.app_id;
            item["action"] = action_to_string(pol.action);
            if (pol.action == ActionType::Blur) item["radius"] = pol.radius;
            if (pol.action == ActionType::Solid) item["color"] = pol.color;
            if (pol.action == ActionType::Pixelate) item["block_size"] = pol.block_size;
            apps_node.push_back(item);
        }

        root["protected_apps"] = apps_node;
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

AppPolicy PrivacyPolicy::get_policy_for_app(const std::string& app_id) const {
    for (const auto& p : policies_) {
        if (p.app_id == app_id || (p.app_id.find('*') != std::string::npos && app_id.find(p.app_id.substr(0, p.app_id.find('*'))) == 0)) {
            return p;
        }
    }
    return AppPolicy{app_id, default_action_, 30, "black", 16};
}

} // namespace wayblur
