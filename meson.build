project('wayblur', 'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2'
  ]
)

gio_dep = dependency('gio-2.0', required: true)
gio_unix_dep = dependency('gio-unix-2.0', required: false)
pipewire_dep = dependency('libpipewire-0.3', required: true)
gstreamer_dep = dependency('gstreamer-1.0', required: true)
gtk4_dep = dependency('gtk4', required: true)
adwaita_dep = dependency('libadwaita-1', required: true)
yaml_dep = dependency('yaml-cpp', required: true)
egl_dep = dependency('egl', required: false)
vulkan_dep = dependency('vulkan', required: false)

inc_dir = include_directories('src')

core_sources = files(
  'src/core/doctor.cpp',
  'src/portal/screencast_manager.cpp',
  'src/renderer/privacy_filter.cpp',
  'src/pipewire/capture_pipeline.cpp',
  'src/policy/privacy_policy.cpp',
  'src/policy/window_tracker.cpp',
  'src/gnome/mutter_integration.cpp',
  'src/security/share_session_monitor.cpp',
  'src/ui/app_window.cpp',
  'src/cli/test_mode.cpp',
  'src/cli/benchmark.cpp',
  'src/cli/cli.cpp'
)

dependencies = [
  gio_dep,
  pipewire_dep,
  gstreamer_dep,
  gtk4_dep,
  adwaita_dep,
  yaml_dep
]

if gio_unix_dep.found()
  dependencies += gio_unix_dep
endif

if egl_dep.found()
  dependencies += egl_dep
endif

if vulkan_dep.found()
  dependencies += vulkan_dep
endif

wayblur_exe = executable('wayblur',
  core_sources + files('src/main.cpp'),
  include_directories: inc_dir,
  dependencies: dependencies,
  install: true
)
