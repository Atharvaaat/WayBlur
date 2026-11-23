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
              << "  protect [--mode data|screen] [--mask-local] [--no-pii] [--ocr-lang <code>]\n"
              << "                       Start active privacy screen-share protection.\n"
              << "                       Viewers get a blurred stream while your own screen stays clear.\n"
              << "                         --mode         data = blur only sensitive data (default),\n"
              << "                                        screen = blur the whole shared screen\n"
              << "                         --mask-local   also cover protected windows on your own screen\n"
              << "                         --no-pii       disable on-screen PII detection (window rules only)\n"
              << "                         --ocr-lang     Tesseract language(s), default eng (e.g. eng+hin)\n"
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
                  << "========================================\n";

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        std::cout << "[WayBlur:CLI] Loading privacy policy configuration...\n";
        PrivacyPolicy policy;
        policy.load_config();

        // Command-line flags override the config file.
        bool mask_local = false;
        bool detect_pii = policy.pii_enabled();
        std::string ocr_lang = policy.ocr_language();
        BlurMode mode = policy.blur_mode();
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--mask-local") mask_local = true;
            else if (args[i] == "--no-pii") detect_pii = false;
            else if (args[i] == "--ocr-lang" && i + 1 < args.size()) ocr_lang = args[++i];
            else if (args[i] == "--mode" && i + 1 < args.size()) {
                std::string m = args[++i];
                if (m == "screen") mode = BlurMode::FullScreen;
                else if (m == "data") mode = BlurMode::DataOnly;
                else { std::cerr << "Unknown --mode '" << m << "' (use data or screen)\n"; return 1; }
            }
        }
        std::cout << "[WayBlur:CLI] Loaded " << policy.policies().size() << " protection rule(s)\n";

        std::cout << "[WayBlur:CLI] Initializing window tracker (Mutter D-Bus)...\n";
        WindowTracker tracker;
        if (tracker.init()) {
            std::cout << "[WayBlur:CLI] Window tracker initialized successfully\n";
        } else {
            std::cerr << "[WayBlur:CLI] Warning: Window tracker init failed (extension may not be active)\n";
        }

        std::cout << "[WayBlur:CLI] Starting share session monitor...\n";
        ShareSessionMonitor monitor;
        monitor.start_monitoring();

        // Pipeline is optional — compositor masks are the primary protection mechanism
        bool pipeline_active = false;
        CapturePipeline pipeline;
        ScreenCastManager portal;
        int pw_fd = -1;
        uint32_t node_id = 0;

        std::cout << "[WayBlur:CLI] Attempting ScreenCast portal session...\n";
        if (portal.init_dbus() && portal.create_session()) {
            portal.select_sources(SourceType::Monitor, CursorMode::Embedded);
            if (portal.start_session()) {
                pw_fd = portal.open_pipewire_remote();
                if (!portal.streams().empty()) {
                    node_id = portal.streams()[0].node_id;
                }
                std::cout << "[WayBlur:CLI] ScreenCast portal session established\n";
            }
        }

        if (pw_fd >= 0 && pipeline.init(pw_fd, node_id) && pipeline.start()) {
            pipeline_active = true;
            std::cout << "[WayBlur:CLI] PipeWire capture pipeline started\n";
            if (mode == BlurMode::DataOnly && !detect_pii) {
                std::cout << "[WayBlur:CLI] PII detection off: only window rules are applied\n";
            } else if (!pipeline.enable_protection(mode, ocr_lang.c_str())) {
                std::cerr << "[WayBlur:CLI] Warning: OCR is not available, so sensitive data cannot be "
                             "detected. Use --mode screen to blur the whole shared screen instead. "
                             "Only window rules are applied for now.\n";
            } else {
                pipeline.set_pii_kinds_mask(policy.pii_kinds_mask());
            }
        } else {
            std::cerr << "[WayBlur:CLI] PipeWire stream pipeline NOT available. "
                         "Falling back to on-screen masks so protected windows stay hidden "
                         "(this also hides them from you).\n";
        }

        // Fail closed: without the stream pipeline, the on-screen mask is the only protection.
        const bool use_local_masks = mask_local || !pipeline_active;

        std::cout << "[WayBlur:CLI] ✓ Protection ACTIVE. Monitoring windows every 100ms...\n"
                  << "[WayBlur:CLI] Press Ctrl+C to stop.\n";

        while (g_running) {
            auto rules = tracker.generate_filter_rules(policy);
            if (pipeline_active) {
                pipeline.set_filter_rules(rules);
            }
            if (use_local_masks) {
                tracker.get_mutter().send_privacy_masks(rules);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\n[WayBlur:CLI] Stopping protection...\n";
        if (pipeline_active) {
            pipeline.stop();
            std::cout << "[WayBlur:CLI] PipeWire pipeline stopped\n";
        }
        monitor.stop_monitoring();
        std::cout << "[WayBlur:CLI] Share session monitor stopped\n";

        if (use_local_masks) {
            std::vector<FilterRule> empty_rules;
            tracker.get_mutter().send_privacy_masks(empty_rules);
            std::cout << "[WayBlur:CLI] Compositor masks cleared\n";
        }
        std::cout << "[WayBlur:CLI] Protection stopped cleanly.\n";
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
