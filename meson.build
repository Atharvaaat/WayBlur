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

tesseract_dep = dependency('tesseract', required: false)
lept_dep = dependency('lept', required: false)
have_ocr = tesseract_dep.found() and lept_dep.found()

inc_dir = include_directories('src')

pii_args = []
pii_deps = []
if have_ocr
  pii_args += '-DWAYBLUR_HAVE_TESSERACT=1'
  # Where the packaged OCR model lives once installed (the .deb copies eng.traineddata here).
  pii_args += '-DWAYBLUR_TESSDATA_DIR="@0@"'.format(get_option('prefix') / get_option('datadir') / 'wayblur' / 'tessdata')
  pii_deps += [tesseract_dep, lept_dep]
else
  warning('Tesseract not found: PII detection will be disabled (install libtesseract-dev libleptonica-dev)')
endif

pii_sources = files(
  'src/pii/pii_classifier.cpp',
  'src/pii/pii_ocr.cpp',
  'src/pii/stream_guard.cpp'
)

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
  core_sources + pii_sources + files('src/main.cpp'),
  include_directories: inc_dir,
  dependencies: dependencies + pii_deps,
  cpp_args: pii_args,
  install: true
)

install_data('data/org.wayblur.desktop.desktop', install_dir: get_option('datadir') / 'applications')
install_data('data/icons/org.wayblur.desktop.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')

# Unit tests (no GTK/PipeWire needed)
test('pii_classifier',
  executable('pii_classifier_test',
    files('src/pii/pii_classifier.cpp', 'tests/pii_classifier_test.cpp'),
    include_directories: inc_dir))

test('stream_guard',
  executable('stream_guard_test',
    files('src/pii/pii_classifier.cpp', 'src/pii/pii_ocr.cpp', 'src/renderer/privacy_filter.cpp',
          'src/pii/stream_guard.cpp', 'tests/stream_guard_test.cpp'),
    include_directories: inc_dir,
    dependencies: pii_deps,
    cpp_args: pii_args),
  timeout: 60)

# Fake-portal protocol test; needs a session bus: dbus-run-session -- meson test portal_fake
test('portal_fake',
  executable('portal_fake_test',
    files('src/portal/screencast_manager.cpp', 'tests/portal_fake_test.cpp'),
    include_directories: inc_dir,
    dependencies: [gio_dep, gio_unix_dep]),
  is_parallel: false)

# Real-OCR "recorder" test. Needs a fixture:
#   python3 tests/make_fixture.py /tmp/screen.pgm && ./build/guard_real_ocr_test /tmp/screen.pgm
guard_real_ocr_test = executable('guard_real_ocr_test',
  files('src/pii/pii_classifier.cpp', 'src/pii/pii_ocr.cpp', 'src/renderer/privacy_filter.cpp',
        'src/pii/stream_guard.cpp', 'tests/guard_real_ocr_test.cpp'),
  include_directories: inc_dir,
  dependencies: pii_deps,
  cpp_args: pii_args)

pii_ocr_e2e = executable('pii_ocr_e2e',
  files('src/pii/pii_classifier.cpp', 'src/pii/pii_ocr.cpp', 'tests/pii_ocr_e2e.cpp'),
  include_directories: inc_dir,
  dependencies: pii_deps,
  cpp_args: pii_args)
