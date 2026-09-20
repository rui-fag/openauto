# OpenAuto (modernized fork)

AndroidAuto™ headunit emulator: turns a Linux PC with a display,
touchscreen, speakers and microphone into an Android Auto head unit for your car.
Plug in an Android phone over USB (or connect over Wi-Fi) and get navigation,
music, calls and voice commands on the big screen.

This is a modernized fork of [f1xpl/openauto](https://github.com/f1xpl/openauto)
(2018, Qt5 / C++14 era), with the [`aasdk`](https://github.com/f1xpl/aasdk)
library vendored under `aasdk/` and ported alongside it so the whole stack
builds on current Linux distributions.

> **Trademarks / license:** Android Auto is a registered trademark of Google Inc.
> This software is **not** certified by Google; it is created for R&D purposes.
> Do not use while driving. You use it at your own risk.
> License: **GNU GPLv3** — see [COPYING](COPYING).
> Copyright (c) 2018 f1x.studio (Michał Szwaj),
> plus later contributors to this fork.

---

## Table of contents

1. [Features](#1-features)
2. [Repository layout](#2-repository-layout)
3. [What changed vs upstream](#3-what-changed-vs-upstream-f1xplopenauto--f1xplaasdk)
4. [Requirements](#4-requirements)
5. [Installing dependencies](#5-installing-dependencies)
6. [Building](#6-building)
7. [Running](#7-running)
8. [Configuration](#8-configuration)
9. [Troubleshooting](#9-troubleshooting)
10. [Development notes](#10-development-notes)

---

## 1. Features

- 480p, 720p and 1080p video at 30 or 60 FPS
- **Hardware-accelerated video decode via VAAPI** (Intel/AMD iGPUs, etc.) through
  FFmpeg, wired into the Qt6 multimedia stack
- Audio playback on all channels (Media, System, Speech) and audio input for
  voice commands — via Qt or **RtAudio** backends (selectable in settings)
- Touchscreen and button input, including scroll wheel and rotary events
- Bluetooth (HFP role handled by the companion `btservice` daemon)
- Automatic launch on device hotplug, automatic detection of connected phones
- Wireless (Wi-Fi) mode via head unit server (enable in the phone's Android Auto
  developer settings first)
- User-friendly settings UI (`SettingsWindow`)
- Legacy path preserved: Raspberry Pi 3 **OpenMAX/OMX** video output
  (`OMXVideoOutput`) selectable with `-DRPI3_BUILD=ON`

Two executables are produced:

| Binary         | Purpose                                                                 |
|----------------|-------------------------------------------------------------------------|
| `bin/autoapp`  | The headunit application (UI + USB/TCP transport + all AA channels)     |
| `bin/btservice`| Bluetooth companion service (RFCOMM server on channel/port 5000 bridging calls) |

---

## 2. Repository layout

```
openauto/
├── CMakeLists.txt            # Top-level build: Qt6 app + bundled aasdk/
├── aasdk/                    # Vendored aasdk library (own git history)
│   ├── CMakeLists.txt
│   ├── aasdk_proto/          # Android Auto .proto sources (+ generated .pb.* at build time)
│   ├── include/f1x/aasdk/    # Public headers (USB, Transport, Messenger, Channels, …)
│   └── src/                  # Library implementation
├── include/f1x/openauto/     # openauto public headers
├── src/
│   ├── autoapp/              # App, Configuration, Projection (audio/video/input),
│   │                         # Service (AA channels), UI (MainWindow, Settings, Connect)
│   └── btservice/            # Bluetooth companion daemon sources
├── assets/                   # Icons + resources.qrc
├── cmake_modules/            # Findlibusb-1.0.cmake, Findrtaudio.cmake
└── .gitignore / aasdk/.gitignore
```

Build outputs go to `bin/` (executables), `lib/` (openauto archives) and
`aasdk/lib/` (`libaasdk.a`, `libaasdk_proto.a`). All of these — plus CMake
caches, Makefiles, Qt autogen dirs and protobuf-generated `*.pb.h/*.pb.cc`
files — are git-ignored (see the `.gitignore` files).

---

## 3. What changed vs upstream (f1xpl/openauto + f1xpl/aasdk)

Upstream is frozen around 2018 (Qt 5, C++14, `cmake_minimum_required(VERSION 3.5.1)`).
This fork brings the stack to a 2025/2026 toolchain. The headline changes:

### 3.1 aasdk vendored into the tree, single unified build

- Upstream keeps `aasdk` as a **separate sibling checkout** (`../aasdk`) that you
  had to configure and build by hand before openauto would even link.
- Here `aasdk/` lives **inside** `openauto/` and is built by the same CMake run
  via `add_subdirectory(aasdk)` (`CMakeLists.txt:16-60`).
- `cmake --build . --target build` compiles everything: `aasdk_proto` →
  `aasdk` → `autoapp` + `btservice`. Plain `cmake --build .` (`all`) and
  `cmake --build . --target clean` also cover both projects.
- Linking is target-based (`aasdk`, `aasdk_proto`) instead of hard-coded
  `../aasdk/lib/*.a` paths, so separate output dirs keep working and shared
  builds (`-DAASDK_BUILD_SHARED=ON`) link correctly.
- Escape hatch preserved: `-DOPENAUTO_USE_BUNDLED_AASDK=OFF` restores the old
  external/prebuilt-`aasdk` behavior via `AASDK_ROOT`.
- `aasdk`/`aasdk_proto` are opted out of Qt `AUTOMOC/AUTOUIC/AUTORCC` (they are
  plain C++/protobuf targets) to silence the
  *"AUTOGEN: No valid Qt version found"* configure warning.

### 3.2 Qt5 → Qt6 multimedia stack

| Upstream (Qt5)                              | This fork (Qt6)                                                        |
|---------------------------------------------|------------------------------------------------------------------------|
| `find_package(Qt5 Multimedia MultimediaWidgets Bluetooth)` | `Qt6` + `Multimedia MultimediaWidgets Bluetooth Quick OpenGL`, `Qt6WaylandClient`, `Qt6MultimediaPrivate`, `Qt6FFmpegMediaPluginImplPrivate`, `EGL`, `VAAPI` |
| `Qt5Multimedia_*` variables                 | `Qt6Multimedia_*` variables                                            |
| Generic Qt video sink                       | `QtVideoOutput` decodes H.264 with **FFmpeg** and uploads frames through **Qt6 private multimedia APIs** (`QtFFmpegMediaPluginImpl/private`, `QtMultimedia/private`) |
| X11 assumed                                 | Explicit `X11`/`Xrandr`/`Xext` libs, **EGL**, and **Wayland client** support; the app prefers the Wayland Qt platform plugin when a Wayland session is detected |

> Note: using Qt's private multimedia headers ties the build to the installed
> Qt 6 minor version (Qt itself warns about this at configure time). Rebuild
> after Qt upgrades.

### 3.3 Hardware video acceleration via VAAPI

- New `QtVideoOutput` pipeline (`src/autoapp/Projection/QtVideoOutput.cpp`)
  negotiates a **VAAPI** hardware device (`av_hwdevice_ctx_create`), picks a
  VAAPI-compatible pixel format, decodes in hardware and downloads frames for
  display, with automatic software fallback.
- At startup (`src/autoapp/autoapp.cpp`) the app sets sane multimedia defaults
  when the user hasn't overridden them:
  `QT_QPA_PLATFORM=wayland` (on Wayland sessions),
  `QT_MEDIA_BACKEND=ffmpeg`, `QT_FFMPEG_DECODING_HW_DEVICE_TYPES=vaapi`, and
  auto-detects `LIBVA_DRIVER_NAME` (`iHD` → `intel` → `i965`).
- The old `OMXVideoOutput` path is untouched and still available for
  Raspberry Pi 3 with `-DRPI3_BUILD=ON`.

### 3.4 Boost modernization (tested with Boost 1.92)

`aasdk` used pre-1.70 Asio idioms that no longer compile:

- `boost::asio::io_service` → `boost::asio::io_context` everywhere
  (`USBHub`, `AOAPDevice`, query chains, transports, `IOContextWrapper`, …).
- Member `io_service::post()/dispatch()` → free functions
  `boost::asio::post()/dispatch()`; strands use `io_context::strand`.
- `C++14 → C++17`, `Boost_USE_STATIC_LIBS ON`, `-DBOOST_ALL_DYN_LINK` removed.
- New `IUSBWrapper` abstraction points to support testing/mocking.

### 3.5 Modern protobuf (36.x) + Abseil

- `find_package(Protobuf)` + `find_package(absl CONFIG REQUIRED)`; `autoapp`
  links `absl::log_internal_check_op` (required by protobuf ≥ 22).
- `aasdk_proto` regenerates cleanly with `protoc` 36.x
  (`TouchEventData`/`TouchLocationData` protos updated for the newer compiler).
- If you mix a distro protobuf with a hand-built Abseil (or vice versa),
  linking will fail — install both from the same source (see §5).

### 3.6 Audio backends: Qt + RtAudio

- New `RtAudioOutput` (`src/autoapp/Projection/RtAudioOutput.cpp`) alongside
  the Qt audio path; backend selectable in the UI
  (`AudioOutputBackendType`: Qt vs RtAudio).
- `RtAudio` is a hard dependency (`find_package(rtaudio REQUIRED)`).

### 3.7 Build system hardening

- `cmake_minimum_required(VERSION 4.0.0)` (CMake 4.x), `CMAKE_BUILD_TYPE`
  defaults to `Release`.
- Release flags: `-O3 -DNDEBUG -flto -march=native -mtune=native
  -ffunction-sections -fdata-sections` + `-Wl,--gc-sections`; `Release` builds
  define `OPENAUTO_LOG_ERRORS_ONLY` / `AASDK_LOG_ERRORS_ONLY` to cut log spam.
- `aasdk` gains `AASDK_BUILD_SHARED` (static default), plus `AASDK_TEST` /
  `AASDK_CODE_COVERAGE` test hooks.

---

## 4. Requirements

- **OS:** Linux (primary). Windows/Raspberry Pi 3 paths exist in CMake but only
  the Linux path is actively maintained here.
- **CPU/GPU:** x86-64 recommended for VAAPI decode (Intel/AMD iGPU); pure
  software decode works anywhere FFmpeg runs.
- **CMake** ≥ 4.0, a C++17 compiler (GCC ≥ 11 / Clang ≥ 13), `pkg-config`.
- **Libraries:**
  - Boost (≥ 1.74 known good; **1.92 verified**) — `system`, `log`
    (+ `unit_test_framework` only for `AASDK_TEST=ON`)
  - Qt 6 — Base, Multimedia (+ FFmpeg backend plugin), MultimediaWidgets,
    Connectivity (Bluetooth), Declarative, OpenGL, Wayland client EGL support
  - FFmpeg dev libs: `libavcodec`, `libavutil`, `libswscale`
    (`libavformat` probe via CMake; `libswresample` for completeness)
  - VAAPI: `libva` (+ `libva-drm`), and a working VA driver (`intel-media-driver`
    `iHD`, `intel-vaapi-driver`, or Mesa's Gallium VA state tracker for AMD)
  - `libusb-1.0`, `OpenSSL`, `protobuf` + `protoc` with matching **Abseil**,
    `RtAudio`, X11 (`libX11`, `libXrandr`, `libXext`), EGL, Wayland client
- **Phone:** Android with Android Auto; a USB cable that does data (not
  charge-only). For wireless mode, enable the head-unit server in the Android
  Auto developer settings on the phone.

---

## 5. Installing dependencies

Install with your distro's package manager. Names differ per distro; the
**Arch** list below is the verified one (Boost 1.92, Qt 6.11, protoc 36.1).
For other distros, match the *library list in §4* if a name below doesn't exist
on your release.

### 5.1 Arch Linux / Manjaro / EndeavourOS (verified)

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf \
  boost boost-libs \
  qt6-base qt6-multimedia qt6-multimedia-ffmpeg qt6-connectivity qt6-declarative \
  qt6-wayland wayland \
  ffmpeg libva libva-utils \
  libusb openssl protobuf abseil-cpp grpc \
  rtaudio \
  libx11 libxrandr libxext libglvnd egl-wayland
```

Check versions after install:

```bash
cmake --version            # >= 4.0
protoc --version           # 36.x expected
qmake6 --version           # Qt 6.x
grep BOOST_LIB_VERSION /usr/include/boost/version.hpp
vainfo                     # should list your VAAPI driver, not "failed"
```

### 5.2 Ubuntu / Debian (24.04 Noble and newer recommended)

Older LTS releases ship Qt 5-era or incomplete Qt 6 multimedia stacks; prefer
24.04+. Adjust names to your release if `apt` reports a package missing.

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  libboost-system-dev libboost-log-dev libboost-test-dev \
  qt6-base-dev qt6-multimedia-dev qt6-connectivity-dev qt6-declarative-dev \
  qt6-wayland libwayland-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
  libva-dev libva-drm2 vainfo \
  libusb-1.0-0-dev libssl-dev \
  protobuf-compiler libprotobuf-dev libabsl-dev \
  librtaudio-dev \
  libx11-dev libxrandr-dev libxext-dev libegl-dev
```

Notes:

- `Qt6MultimediaPrivate` / `Qt6FFmpegMediaPluginImplPrivate` CMake configs ship
  with `qt6-multimedia-dev`; if your release splits them out, install the
  matching `-dev` package that provides them.
- If `libabsl-dev` is missing/older than what `libprotobuf-dev` needs, either
  take both from the same release or build Abseil + protobuf from source
  together — never mix major versions.

### 5.3 Fedora (41+)

```bash
sudo dnf install -y \
  gcc gcc-c++ cmake ninja-build pkgconf-pkg-config \
  boost-devel \
  qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qtconnectivity-devel \
  qt6-qtdeclarative-devel qt6-qtwayland wayland-devel \
  ffmpeg-devel libva-devel libva-utils \
  libusb1-devel openssl-devel \
  protobuf-devel protobuf-compiler abseil-cpp-devel grpc-devel \
  rtaudio-devel \
  libX11-devel libXrandr-devel libXext-devel mesa-libEGL-devel
```

RPM Fusion may be needed for full `ffmpeg-devel` on some spins.

### 5.4 openSUSE Tumbleweed / Leap 15.6+

```bash
sudo zypper install -y \
  gcc gcc-c++ cmake ninja pkgconf \
  boost-devel libboost_system-devel libboost_log-devel \
  qt6-base-devel qt6-multimedia-devel qt6-connectivity-devel \
  qt6-declarative-devel qt6-wayland wayland-devel \
  ffmpeg-devel libva-devel libva-utils \
  libusb-1_0-devel libopenssl-devel \
  protobuf-devel abseil-cpp-devel \
  rtaudio-devel \
  libX11-devel libXrandr-devel libXext-devel Mesa-libEGL-devel
```

### 5.5 Verifying the toolchain

```bash
cmake --version && protoc --version
pkg-config --modversion libavcodec libavutil libswscale
vainfo | head -20        # want: driver + supported profiles (e.g. H264)
ls /usr/lib/dri/*_drv_video.so 2>/dev/null
```

If `vainfo` fails, video still works (software decode) but at higher CPU cost —
see [No VAAPI / high CPU usage](#no-vaapi--high-cpu-usage).

---

## 6. Building

### 6.1 Quick start (in-source, historical layout)

```bash
cd openauto
cmake -DCMAKE_BUILD_TYPE=Release .
cmake --build . --target build -j$(nproc)
```

This single invocation configures **and** builds, in order:
`aasdk_proto` → `aasdk` → `autoapp` + `btservice`.
Artifacts: `bin/autoapp`, `bin/btservice`, `aasdk/lib/libaasdk.a`,
`aasdk/lib/libaasdk_proto.a`.

### 6.2 Out-of-source build (keeps the tree clean)

```bash
cd openauto
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target build -j$(nproc)
```

> Note: runtime/library output paths are pinned to the source tree
> (`bin/`, `aasdk/lib/`) by design, so binaries still land there even with
> an out-of-source build dir. Only CMake's own scratch files stay in `build/`.

### 6.3 Useful targets and options

```bash
cmake --build . --target help | grep -E "autoapp|btservice|aasdk|build|clean"
cmake --build . --target clean        # cleans openauto AND aasdk objects/bins
cmake --build . --target autoapp      # just the headunit app
cmake --build . --target btservice    # just the bluetooth daemon
```

| CMake option                | Default | Meaning                                                        |
|-----------------------------|---------|----------------------------------------------------------------|
| `OPENAUTO_USE_BUNDLED_AASDK` | `ON`   | Build `aasdk/` as part of this build (recommended)             |
| `CMAKE_BUILD_TYPE`          | `Release` | `Release` enables LTO + error-only logging; use `Debug` for `-g -O0` + full logs |
| `RPI3_BUILD`                | `OFF`   | `ON` selects the OpenMAX/OMX video path for Raspberry Pi 3     |
| `AASDK_BUILD_SHARED`        | `OFF`   | `ON` builds `aasdk`/`aasdk_proto` as `.so` instead of `.a`     |
| `AASDK_TEST`                | `OFF`   | `ON` builds `aasdk_ut` unit tests (needs GTest/GMock)          |
| `AASDK_ROOT`, `AASDK_LIBRARIES`, … | — | Only used with `OPENAUTO_USE_BUNDLED_AASDK=OFF` for an external prebuilt aasdk |

Reconfigure after changing options, e.g.:

```bash
cmake -DRPI3_BUILD=ON -DCMAKE_BUILD_TYPE=Release .
cmake --build . --target build -j$(nproc)
```

### 6.4 Clean rebuild (when switching branches/options)

```bash
cd openauto
rm -rf CMakeCache.txt CMakeFiles cmake_install.cmake Makefile \
       .qt autoapp_autogen btservice_autogen \
       aasdk/CMakeCache.txt aasdk/CMakeFiles aasdk/cmake_install.cmake aasdk/Makefile \
       aasdk/aasdk_proto/CMakeFiles aasdk/aasdk_proto/Makefile aasdk/aasdk_proto/cmake_install.cmake \
       aasdk/aasdk_proto/*.pb.cc aasdk/aasdk_proto/*.pb.h
cmake -DCMAKE_BUILD_TYPE=Release .
cmake --build . --target build -j$(nproc)
```

(`bin/`, `lib/`, `aasdk/lib/`, `tags` and the autostart helpers below are
covered by `.gitignore`, so `git status` stays clean.)

---

## 7. Running

### 7.1 USB permissions (do this once)

The app talks to the phone over USB (libusb hotplug + AOAP accessory mode),
so your user needs device access. Create a udev rule:

```bash
sudo tee /etc/udev/rules.d/51-android-auto.rules > /dev/null <<'EOF'
# Google / Android accessory-mode VID, plus common phone vendors
SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="04e8", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="0bb4", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="12d1", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="22b8", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="2a70", MODE="0666", GROUP="plugdev"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -aG plugdev "$USER"   # log out/in afterwards
```

Also make sure no other tool (e.g. `adb`, other headunit apps) is holding the
phone's USB interface.

### 7.2 Start the app

```bash
cd openauto
./bin/btservice &   # only needed for Bluetooth calling features
./bin/autoapp
```

Then:

1. **USB mode:** plug the phone in with a data-capable cable. `autoapp` should
   detect the hotplug event, run the AOAP handshake and show the Android Auto
   UI. Accept the prompts on the phone.
2. **Wireless mode:** on the phone, open Android Auto settings → tap the version
   row repeatedly to unlock developer settings → enable *head unit server*,
   note the phone's IP. In `autoapp`, use the connect dialog to reach it.

On first run, open **Settings** in the app: pick resolution/FPS
(480p/720p/1080p @ 30/60), audio backend (Qt vs RtAudio), and input handedness
as needed for your car/PC setup.

### 7.3 Environment knobs (optional)

The app pre-sets these only when unset, so you can always override:

| Variable                              | Default set by app | Purpose                                  |
|---------------------------------------|--------------------|------------------------------------------|
| `QT_QPA_PLATFORM`                     | `wayland` (on Wayland sessions) | Force `wayland`/`xcb` Qt platform |
| `QT_MEDIA_BACKEND`                    | `ffmpeg`           | Qt6 multimedia backend                   |
| `QT_FFMPEG_DECODING_HW_DEVICE_TYPES`  | `vaapi`            | Prefer VAAPI HW decode                   |
| `LIBVA_DRIVER_NAME`                   | auto (`iHD`/`intel`/`i965`) | VA driver override               |

Example — force X11 + software decode for testing:

```bash
QT_QPA_PLATFORM=xcb QT_FFMPEG_DECODING_HW_DEVICE_TYPES= ./bin/autoapp
```

### 7.4 Raspberry Pi 3 notes

- Build with `-DRPI3_BUILD=ON` (needs the Broadcom `ilclient`/OpenMAX IL stack
  under `/opt/vc`).
- Upstream targeted 1080p@60 via OMX hardware decode; on modern Pi OS images
  the `/opt/vc` firmware stack may be absent — the standard Qt/FFmpeg path is
  the maintained one on desktop Linux.

---

## 8. Configuration

Settings persist via the in-app Settings window (resolution, FPS, audio
backend, handedness of traffic, recent Wi-Fi addresses, …). Command-line
surface is minimal — both binaries are plain Qt apps:

```bash
./bin/autoapp --help     # Qt standard options
./bin/btservice          # listens on all local BT adapters, RFCOMM port 5000
```

Debug builds (`-DCMAKE_BUILD_TYPE=Debug`) keep full `OPENAUTO_LOG` /
`AASDK_LOG` verbosity; `Release` builds compile in `*_LOG_ERRORS_ONLY`.

---

## 9. Troubleshooting

**CMake can't find Qt6 private configs**
(`Qt6MultimediaPrivate`, `Qt6FFmpegMediaPluginImplPrivate`, `Qt6WaylandClient`)
→ install the full `qt6-multimedia-dev` + Wayland packages for your distro
(§5) and make sure only one Qt 6 series is on `CMAKE_PREFIX_PATH`. If you
upgraded Qt, wipe the CMake cache (§6.4) and rebuild.

**`AUTOGEN: No valid Qt version found` for aasdk targets**
→ already handled in this tree (aasdk targets opt out of AUTOMOC, see §3.1).
If you see it, you are configuring a stale cache — clean-rebuild per §6.4.

**`aasdk_proto/*.pb.h: No such file`** (openauto compile fails)
→ `aasdk_proto` didn't build/generate first. With
`OPENAUTO_USE_BUNDLED_AASDK=ON` this can't happen via the `build`/`all`
targets; if you built targets manually out of order, run
`cmake --build . --target build`.

**`absl::log_internal_check_op` link errors**
→ protobuf/Abseil version mismatch. Install both from the same distro release
(or build both from source together); never mix.

**Boost errors about `io_service` / `post` / `strand`**
→ you're compiling old aasdk sources with new Boost. This tree is already
ported (`io_context`, free-function `post`/`dispatch`); make sure
`openauto/aasdk` is the vendored copy, not an old external checkout
(`OPENAUTO_USE_BUNDLED_AASDK=ON`, the default).

**Phone not detected over USB**
→ check cable (data, not charge-only), udev rule + `plugdev` group (§7.1),
`dmesg` on plug, kill competing `adb`/headunit processes, try another port
(avoid hubs for the first test).

**No VAAPI / high CPU usage**
→ run `vainfo`; install the right VA driver (`intel-media-driver` for modern
Intel, `intel-vaapi-driver` for old, Mesa VA for AMD) and set
`LIBVA_DRIVER_NAME` if autodetect picks wrong. Software decode is the
automatic fallback — it works, just hotter.

**Bluetooth calls don't work**
→ `btservice` must be running *before* the call starts, and BlueZ must own the
adapter (`bluetoothctl` shows the controller up). Pair from the phone side.

**Wayland vs X11 glitches**
→ force one explicitly: `QT_QPA_PLATFORM=wayland ./bin/autoapp` or
`QT_QPA_PLATFORM=xcb ./bin/autoapp`.

---

## 10. Development notes

- Warnings policy: `-Wall -pedantic -fPIC` globally; treat new warnings as bugs.
- Release LTO uses `-march=native`: binaries built on one CPU may not run on an
  older one — override `CMAKE_CXX_FLAGS_RELEASE` for portable binaries.
- `aasdk/` keeps its own git history (nested repo). Commit library changes
  inside `aasdk/` and integration changes in `openauto/` separately.
- Regenerating protobuf: `protoc` runs automatically via
  `protobuf_generate_cpp`; never hand-edit `aasdk_proto/*.pb.*` (they're
  git-ignored build outputs).
- Tests: `cmake -DAASDK_TEST=ON . && cmake --build . --target aasdk_ut` builds
  the aasdk unit-test binary (needs GTest/GMock).
