#include "cli/cli.hpp"
#include "core/doctor.hpp"
#include "portal/screencast_manager.hpp"
#include "policy/privacy_policy.hpp"
#include "policy/window_tracker.hpp"
#include "security/share_session_monitor.hpp"
#include "pipewire/capture_pipeline.hpp"
#include "cli/test_mode.hpp"
#include "cli/benchmark.hpp"
#include "ui/app_window.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <csignal>

namespace wayblur {

namespace {

std::atomic<bool> g_running{true};

void handle_signal(int) {
    g_running = false;
}

void print_usage() {
    std::cout << "WayBlur — Privacy-Aware Screen Sharing for GNOME / Wayland\n\n"
              << "Usage:\n"
              << "  wayblur <command> [options]\n\n"
              << "Commands:\n"
              << "  doctor               Run system environment diagnostics\n"
              << "  capture [--source monitor|window] Start screen capture stream\n"
              << "  policy               View active privacy policies\n"
              << "  policy --add <target> [--type app|dns] [--action blur|solid|pixelate]  Add a policy rule\n"
              << "  policy --remove <target>  Remove a policy rule\n"
              << "  protect              Start active privacy screen-share protection\n"
              << "  test                 Run synthetic protection test mode\n"
              << "  benchmark            Profile pipeline latency and performance\n"
              << "  gui                  Launch GTK4 / Libadwaita user interface\n"
              << "  --help, -h           Show this help message\n";
}

} // namespace

int run_cli(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (args.empty() || args[0] == "doctor") {
        DoctorReport report = run_diagnostics();
        print_doctor_report(report);
        return 0;
    }

    if (args[0] == "capture") {
        SourceType source = SourceType::Monitor;
        if (args.size() >= 3 && args[1] == "--source") {
            if (args[2] == "window") source = SourceType::Window;
            else if (args[2] == "monitor") source = SourceType::Monitor;
        }
        std::cout << "[WayBlur] Initiating ScreenCast capture stream (Source: "
                  << (source == SourceType::Window ? "Window" : "Monitor") << ")...\n";

        ScreenCastManager mgr;
        if (!mgr.init_dbus() || !mgr.create_session()) {
            std::cerr << "[WayBlur] Failed to initialize ScreenCast portal session.\n";
            return 1;
        }
        mgr.select_sources(source, CursorMode::Embedded);
        std::cout << "[WayBlur] Portal session established: " << mgr.session_handle() << "\n";
        return 0;
    }

    if (args[0] == "policy") {
        PrivacyPolicy policy;
        policy.load_config();

        if (args.size() >= 3 && args[1] == "--add") {
            std::string target = args[2];
            std::string type = "app";
            ActionType act = ActionType::Blur;

            for (size_t i = 3; i < args.size(); ++i) {
                if (args[i] == "--type" && i + 1 < args.size()) {
                    type = args[++i];
                } else if (args[i] == "--action" && i + 1 < args.size()) {
                    std::string a = args[++i];
                    if (a == "solid") act = ActionType::Solid;
                    else if (a == "pixelate") act = ActionType::Pixelate;
                    else act = ActionType::Blur;
                }
            }

            AppPolicy pol;
            pol.app_id = target;
            pol.type = type;
            pol.action = act;
            pol.radius = 30;
            pol.color = "black";
            pol.block_size = 16;

            policy.add_policy(pol);
            if (policy.save_config()) {
                std::cout << "✓ Added protection rule for target '" << target << "' [" << type << "].\n";
            } else {
                std::cerr << "Failed to save policy configuration.\n";
            }
            return 0;
        }

        if (args.size() >= 3 && args[1] == "--remove") {
            std::string target = args[2];
            if (policy.remove_policy(target)) {
                policy.save_config();
                std::cout << "✓ Removed protection rule for target '" << target << "'.\n";
            } else {
                std::cout << "Rule for target '" << target << "' not found.\n";
            }
            return 0;
        }

        std::cout << "WayBlur Configured Application & DNS Protection Policies:\n"
                  << "Config file: " << PrivacyPolicy::get_default_config_path() << "\n\n";

        for (const auto& p : policy.policies()) {
            std::cout << "  - Target: " << p.app_id << " [" << (p.type == "dns" ? "DNS/Domain" : "App") << "]\n"
                      << "    Action: " << (p.action == ActionType::Solid ? "solid" : (p.action == ActionType::Blur ? "blur" : "pixelate")) << "\n"
                      << "    Radius: " << p.radius << " | Color: " << p.color << "\n\n";
        }
        return 0;
    }

    if (args[0] == "protect") {
        std::cout << "========================================\n"
                  << "      WayBlur Privacy Protection        \n"
                  << "========================================\n"
                  << "Status: ACTIVE & PROTECTING\n"
                  << "Fail-Closed Security: ACTIVE\n"
                  << "Press Ctrl+C to stop protection.\n";

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        PrivacyPolicy policy;
        policy.load_config();

        WindowTracker tracker;
        tracker.init();

        ShareSessionMonitor monitor;
        monitor.start_monitoring();

        // Establish ScreenCast portal session for PipeWire stream
        ScreenCastManager portal;
        int pw_fd = -1;
        uint32_t node_id = 0;
        if (portal.init_dbus() && portal.create_session()) {
            portal.select_sources(SourceType::Monitor, CursorMode::Embedded);
            if (portal.start_session()) {
                pw_fd = portal.open_pipewire_remote();
                if (!portal.streams().empty()) {
                    node_id = portal.streams()[0].node_id;
                }
            }
            std::cout << "[WayBlur] ScreenCast session: " << portal.session_handle() << "\n";
        } else {
            std::cerr << "[WayBlur] Warning: Could not establish ScreenCast portal session.\n";
        }

        CapturePipeline pipeline;
        pipeline.init(pw_fd, node_id);
        pipeline.start();

        std::cout << "[WayBlur] Protection loop active. Monitoring windows...\n";

        while (g_running) {
            auto rules = tracker.generate_filter_rules(policy);
            pipeline.set_filter_rules(rules);
            // Send compositor-level privacy masks to GNOME Shell extension
            tracker.get_mutter().send_privacy_masks(rules);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        pipeline.stop();
        monitor.stop_monitoring();
        // Clear compositor masks on exit
        std::vector<FilterRule> empty_rules;
        tracker.get_mutter().send_privacy_masks(empty_rules);
        std::cout << "\n[WayBlur] Protection stopped cleanly.\n";
        return 0;
    }

    if (args[0] == "test") {
        TestMode test_mode;
        TestModeResult res = test_mode.run_test();
        std::cout << res.summary << "\n";
        return res.passed ? 0 : 1;
    }

    if (args[0] == "benchmark") {
        Benchmark bm;
        auto reports = bm.run_benchmark();
        bm.print_report(reports);
        return 0;
    }

    if (args[0] == "gui") {
        AppWindow window;
        return window.run(argc, argv);
    }

    if (args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        print_usage();
        return 0;
    }

    std::cerr << "Unknown command: " << args[0] << "\n\n";
    print_usage();
    return 1;
}

} // namespace wayblur
