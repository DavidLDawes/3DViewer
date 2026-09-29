# CLAUDE.md

Guidance for Claude when working in this repository.

## What this is

`DavidLDawes/3DViewer` is a fork of [Revopoint/3DViewer](https://github.com/Revopoint/3DViewer),
the open-source Qt5 viewer for Revopoint 3D cameras (POP, MINI, RANGE, ...). In the
`Controller` umbrella it plays the role `core`/`RP2040` play for grblHAL: the
device-side code that the MCP layer (`mhs2revo`, sibling directory) builds on. It is
the reference for what the camera can report, how to control it, how to read its
status, and what results it produces.

Keep the fork mergeable with upstream: minimal changes, upstream style (4-space indent,
`CS`/`cs::` naming, Qt signal/slot idioms, the GPL header on new files). Fork-only
additions so far: `CLAUDE.md`, `src/csbridge/` (revo-bridge), `src/tests/`,
`.github/workflows/ci.yml`, the `BUILD_BRIDGE`/`BUILD_TESTS` options in
`src/CMakeLists.txt`, and the fork section of `README.md`. Keep fork code in its own
directories so upstream merges stay trivial.

## Layout

| Path | What |
| --- | --- |
| `src/CMakeLists.txt` | Top-level CMake (configure from `src/`, not the repo root) |
| `src/csutil/` | `csutil.dll/.so`: logging, 16-bit PNG (libpng), Qt helpers |
| `src/cscamera/` | `cscamera.dll/.so`: the camera layer. `CSCamera` wraps the SDK (`cscamera.cpp`), `CameraThread` runs it, `process/` turns frames into depth/RGB/point-cloud output, `CameraCaptureTool`/`OutputSaver` save captures, `CapturedZipParser`/`FormatConverter` re-export saved `.zip` captures |
| `src/csviewer/` | The Qt GUI (`3DViewer` executable), OSG 3D rendering |
| `src/csbridge/` | `revo-bridge` (fork addition): JSON-lines helper process over the SDK, used by mhs2revo. Protocol and design in `src/csbridge/README.md` |
| `src/tests/` | Hardware-free Qt Test / CTest tests (fork addition) |
| `thirdparty/3DCamera/` | **Prebuilt, closed-source** Revopoint SDK (v3.2.23): headers + `3DCamera.dll`/`.lib`, `lib3DCamera.so`, `lib3DCamera.dylib` |
| `thirdparty/{osg3.6.5,yaml-cpp0.6.0,quazip,libpng}` | Prebuilt dependencies per platform |
| `scripts/` | udev rules (Linux) and the macOS RPC helper installer for USB cameras |

## The SDK is the real contract

Everything the camera can do goes through `thirdparty/3DCamera/include`:

- **C++ API** (`hpp/`): `cs::getSystemPtr()` -> `ISystem` (enumerate, hot-plug and alarm
  callbacks), `cs::getCameraPtr()` -> `ICamera` (connect, streams, `softTrigger`,
  properties, intrinsics/extrinsics, `restart`).
- **C API** (`h/`, exported from the same library): `createSystem`,
  `systemConnectCamera`, `cameraStartStream`, `cameraGetFrame`, `cameraSoftwareTrigger`,
  `cameraSetPropertyExtension`, ... Usable from other languages, but several calls pass
  the large `PropertyExtension` union by value and one takes a C++ reference.
- **Header-only processing** (`hpp/Processing.hpp`): `cs::Pointcloud::generatePoints()`
  (depth + intrinsics -> points + normals), `exportToFile()` (PLY, ascii or binary,
  optional RGB), and an unused `PointcloudWithMesh` (single-view mesh PLY).
- Property IDs (`PROPERTY_TYPE`, `PROPERTY_TYPE_EXTENSION`), `ERROR_CODE`, `CameraType`,
  stream formats live in `hpp/Types.hpp`. Treat those numbers as the SDK's contract; look
  them up there, not from memory.

What the SDK does **not** provide: turntable control, multi-view registration/fusion,
or watertight mesh output. Those live in Revopoint's closed Revo Scan app, not here.
The SDK has LED/laser controls (`PROPERTY_EXT_LED_ON_OFF`, `PROPERTY_EXT_LED_CTRL` with
`IR_LED`/`RGB_LED`/`LASER_LED`, `PROPERTY_EXT_LASER_ON_OFF`/`_BRIGHTNESS`) that 3DViewer
itself never calls, so which models honor them is unverified.

## Build and test

- Needs Qt 5.10+ (CI and the local bench use 5.15.2), CMake 3.10+, MSVC 2015+ / g++.
  OpenMP is optional (`-DUSE_OPENMP=ON`).
- Windows (bench machine: VS 2022, Qt at `C:\Qt\5.15.2\msvc2019_64`, CMake from
  `pip install cmake`):

  ```
  cmake -S src -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTS=ON -DQt5_DIR=C:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5
  cmake --build build --config Release --parallel
  ctest --test-dir build -C Release --output-on-failure
  ```

- Linux: same, with the qt.io Qt 5.15.2 `gcc_64` build (not Ubuntu's `qtbase5-dev`:
  the prebuilt quazip needs a symbol only qt.io's Qt exports) and `-G Ninja
  -DCMAKE_BUILD_TYPE=Release`. See `.github/workflows/ci.yml`.
- **Configuring rewrites tracked files**: `src/csviewer/CMakeLists.txt` runs `lupdate`/
  `lrelease` at configure time, which touches `src/csviewer/translations/*.ts/.qm`.
  Don't commit those unless you meant to change translations
  (`git checkout -- src/csviewer/translations`).
- The build does not copy runtime DLLs. To run `build/bin/3DViewer.exe` put Qt's `bin`
  and `thirdparty/{3DCamera/windows/x64,osg3.6.5/windows/bin,yaml-cpp0.6.0/windows/bin,quazip/windows/bin,libpng/windows/bin}`
  on `PATH`. CTest sets the paths it needs for the tests itself.
- On Windows, Qt Test output may not show in a non-console shell; run a test exe with
  `-o out.txt,txt` to capture it.
- macOS isn't built in CI: the bundled mac binaries are x86_64 only.

## Tests

`src/tests/` (built with `-DBUILD_TESTS=ON`, run by CI on Linux and Windows) needs no
camera:

- `test_pointcloud` - depth -> point cloud math and PLY export from `Processing.hpp`.
- `test_imageutil` - lossless 16-bit depth PNG round trip through `csutil`.
- `test_sdk_smoke` - the prebuilt SDK loads, reports its version, enumerates (0 cameras
  in CI), and refuses trigger/property calls when nothing is connected. It never
  connects to or changes a camera, even if one is attached.
- `test_bridge` - revo-bridge's protocol, property table and capture writer against a
  fake camera backend (`FakeBackend` in the test).
- `revo_bridge_selftest` - the real `revo-bridge --selftest` starts and enumerates.

These do not replace testing with a real camera. **Say plainly when something was not
verified on hardware**; as of this writing nothing in this fork has been run against a
camera.

## revo-bridge notes

- `BridgeServer` has no stdio and no SDK library dependency; everything camera-facing
  goes through `CameraBackend`, so it is tested with a fake. Keep it that way: new
  commands go in `BridgeServer`, new SDK calls go in `CameraBackend` + `SdkBackend` +
  the test's `FakeBackend`.
- `PropertyExtension` is a **union**: zero it, then set only the one field for the
  property being written. A fake that stores one shared union gets wrong values back,
  because the fields overlap (a real bug in the first version of the test).
- Every property the bridge exposes is in `propertymap.cpp`'s allow-list, with
  `usedBy3DViewer` saying whether 3DViewer itself exercises it. Keep that flag honest.
- stdout is protocol-only (`main.cpp` moves the real stdout aside and points fd 1 at
  stderr). Log to stderr.
- The protocol is versioned (`PROTOCOL_VERSION`). mhs2revo depends on it, so bump the
  version for incompatible changes and update `src/csbridge/README.md`.

## Practices

- Follow the umbrella `../CLAUDE.md` (repo boundaries, honest scope, safety).
- Commit here only for files this repo owns; `mhs2revo` changes are separate commits in
  that repo.
- Don't modify `thirdparty/` binaries; if the SDK is upgraded, record the version and
  rerun `test_sdk_smoke`.
