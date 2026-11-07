# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

MusiKloud2 is a Qt 4 SoundCloud client and music player, extensible via service plugins (originally by Stuart Howarth / marxoft, GPLv3). One codebase targets three UIs: Symbian^3 (QML / Qt Quick Components), Maemo 5 (Qt widgets + Hildon), and Linux desktop (Qt widgets). Current work in this fork is focused on the **Symbian** build.

There are no tests and no linter.

## Building (Symbian)

Requires the Qt SDK at `C:\QtSDK` (Symbian^3 Qt 4.7.4 target, GCCE) and the qsoundcloud submodule:

    git submodule update --init
    build_symbian.bat            (release, default)
    build_symbian.bat debug

This produces an unsigned `musikloud2.sis` in the repo root. Always build from the top-level `musikloud2.pro` (that is what the script does), not `app/app.pro`: the top-level project builds `external/qsoundcloud_lib` first.

- The build is **in-source** (Qt 4.7 + sbsv2 does not support shadow builds). qmake and sbsv2 output (`Makefile*`, `bld.inf`, `*.mmp`, `*.pkg`, `moc_*.cpp`, `.make.cache`, `*.sis`, ...) lands next to the `.pro` files and is gitignored. Do not edit or commit these files.
- `.cpp` files and QML files must be listed explicitly in `app/app.pro`. QML/image files are deployed through the `qml_base`, `qml_plugins`, `qml_soundcloud` and `images` `DEPLOYMENT` lists to `!:/Private/e77abf1c/qml/...`, so a new QML file that isn't listed there will be missing on the device.

### qsoundcloud dependency

- `external/qsoundcloud` is a git submodule (the user's fork of marxoft/qsoundcloud).
- `external/qsoundcloud_lib/qsoundcloud_lib.pro` builds it as a **static library** (`QSOUNDCLOUD_STATIC_LIBRARY`) that is linked into the app. It is a static lib rather than sources compiled into `app.pro` because both projects have a `resourcesrequest.h`, and two `moc_resourcesrequest.cpp` files in one Symbian MMP collide. qsoundcloud's own `json.cpp` is left out because the app already provides the same QtJson (`app/src/base/json.cpp`).
- `external/include/qsoundcloud/*.h` are forwarding headers so app code can `#include <qsoundcloud/foo.h>`. Add a forwarder there if you start using another qsoundcloud header.
- The Maemo 5 and desktop builds link a system-installed `libqsoundcloud` through pkg-config instead.

## Architecture

All of `app/` is one qmake project (`app/app.pro`) with per-platform scopes (`maemo5`, `symbian`, `unix`).

**Shared, platform-independent code** (compiled for every target):
- `src/base/`: abstract domain objects (`MKTrack`, `MKArtist`, `MKPlaylist`, `MKComment`), list models, the download queue (`Transfers`/`TransferModel`), `Resources`, `Utils` and QtJson.
- `src/audioplayer/`: the `AudioPlayer` singleton (QtMobility/QtMultimedia playback queue).
- `src/soundcloud/`: SoundCloud implementations (`SoundCloudTrack : MKTrack`, models, accounts) built on qsoundcloud.
- `src/plugins/`: the plugin system (see below), with `Plugin*` counterparts of the SoundCloud classes (`PluginTrack : MKTrack`, ...).

**Per-platform code**: `src/symbian/`, `src/maemo5/`, `src/desktop/`. Each one provides its own `main.cpp` and headers with the **same names**: `definitions.h` (paths, plugin dirs, `VERSION_NUMBER`, limits), `settings.h`, `logger.h`, `database.h` and `transfer.h`. Shared code includes them by bare name, and the platform's `INCLUDEPATH` decides which one is used. A change to one of these interfaces usually has to be mirrored in all three platforms.

**Symbian UI**: `src/symbian/main.cpp` registers the C++ models and objects with `qmlRegisterType` under the `MusiKloud 2.0` import (qsoundcloud request types under `QSoundCloud 1.0`). It exposes the singletons as context properties (`player`, `settings`, `soundcloud`, `plugins`, `transfers`, `utils`, `mkresources`, `clipboard`, `logger`, `volumekeys`) and loads `qml/main.qml` from the application directory at runtime (not from a .qrc). The QML lives in `src/symbian/qml/`, with `soundcloud/` and `plugins/` subfolders that mirror the C++ split. The version number appears in both `app.pro` (`VERSION`) and `src/symbian/definitions.h` (`VERSION_NUMBER`).

### Plugin system

`PluginManager::load()` scans `PLUGIN_PATHS` (on Symbian: `C:/`, `E:/`, `F:/` + `musikloud2/plugins/`) for `*.json` configs. `ServicePluginConfig` reads the `type` field:
- `"qt"`: a Qt plugin library (`LIB_PREFIX + id + LIB_SUFFIX`, `.qtplugin` on Symbian) that implements the `ServicePlugin` interface (`org.musikloud2.ServicePlugin`) and returns a `ResourcesRequest`.
- `"js"`: `JavaScriptServicePlugin` runs `<id>.js` in QtScript, with an `XMLHttpRequest` implementation (`src/plugins/xmlhttprequest.cpp`) and a global request object.
- anything else: `ExternalServicePlugin` runs an external executable `<id>.<type>`.

The JSON `resources` array declares which `method` (`list` / `search` / `get`) and `type` (`track`, `artist`, `playlist`, `category`, ...) the service supports. URL handling uses `regExp`. Example plugins are in `plugins/mixcloud` and `plugins/internetradio` (JS); `plugins/src/` holds the public headers for writing Qt plugins.
