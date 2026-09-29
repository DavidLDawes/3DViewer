# revo-bridge

A small console program that drives a Revopoint camera through the 3DCamera SDK and
speaks **JSON lines** on stdin/stdout. It exists so that
[mhs2revo](https://github.com/DavidLDawes/mhs2revo) (a Python MCP server) can control
the camera without binding the SDK's C++ ABI from Python. It is a fork addition, not
part of upstream 3DViewer, and it doesn't use the GUI or `cscamera`: it calls the SDK
directly, the same way `cscamera` does.

**Status:** it builds and passes its tests on Windows and Linux with no camera.
**Nothing has been run against a real camera yet** (target: POP 2).

## Running

```
revo-bridge [--selftest] [--sdk-log DIR] [--no-networking] [--version]
```

- stdout carries only protocol lines. Anything else that writes to stdout, such as the
  SDK or Qt, is redirected to stderr, and so are the bridge's own diagnostics.
- `--selftest` prints the `ready` event and a `list` reply, then exits (0 on success).
- `--sdk-log DIR` turns on the SDK's own log files. They are off by default.
- `--no-networking` calls `cs::setSdkEnableNetworking(false)` first. Whether USB cameras
  still work with it is untested.
- When stdin closes, the bridge stops streaming and disconnects before exiting.
  `shutdown` does the same.
- Windows: the build copies `3DCamera.dll` and `libpng16.dll` next to the exe
  (`build/bin`). Qt's DLLs must be on `PATH`, or deployed with `windeployqt`.

## Protocol (version 1)

One compact JSON object per line, UTF-8.

```
-> {"id": 1, "cmd": "connect", "args": {"serial": "..."}}
<- {"id": 1, "ok": true, "result": {...}}
<- {"id": 1, "ok": false, "error": {"code": "SDK_ERROR", "message": "...", "sdk_code": 7, "sdk_error": "FRAME_TIMEOUT"}}
<- {"event": "camera_removed", "serial": "..."}
```

- The first line is always `{"event":"ready","protocol":1,"bridge_version":...,"sdk_version":...}`.
- Requests are handled one at a time, and each gets exactly one reply with the same
  `id`. `id` can be any JSON value.
- Events have no `id` and can arrive between replies: `camera_added`, `camera_removed`
  (`serial`), and `alarm` (`data`, or `raw` if it isn't JSON; the SDK doesn't document
  the schema).
- Error `code`s: `BAD_REQUEST`, `UNKNOWN_COMMAND`, `BAD_VALUE`, `NOT_CONNECTED`,
  `NOT_STREAMING`, `NOT_SUPPORTED`, `ALREADY_CONNECTED`, `ALREADY_STREAMING`,
  `CAMERA_NOT_FOUND`, `AMBIGUOUS_CAMERA`, `CAMERA_IN_USE`, `UNKNOWN_PROPERTY`,
  `NOT_READABLE`, `NOT_WRITABLE`, `SDK_ERROR`. `SDK_ERROR` also carries the SDK's
  `ERROR_CODE` as `sdk_code` and a name as `sdk_error` (`DEVICE_NOT_FOUND`,
  `FRAME_TIMEOUT`, `NOT_SUPPORT`, ...).

### Commands

| cmd | args | result |
|-----|------|--------|
| `hello` | - | `protocol`, `bridge_version`, `sdk_version` |
| `list` | - | `cameras`: `name`, `serial`, `unique_id`, `firmware_version`, `algorithm_version`, `model`, `connection` (usb/network), `status` |
| `connect` | `serial?` (optional if exactly one camera) | camera fields plus `depth_supported`, `rgb_supported`, `warnings?`. Reads calibration, and sets the depth/RGB frame matching the way 3DViewer does |
| `disconnect` | - | `was_connected` |
| `info` | - | camera fields, versions, active streams, `depth_intrinsics`, `rgb_intrinsics`, `extrinsics` |
| `capabilities` | - | `streams.depth/rgb`: `supported` and `modes` [{format, width, height, fps}]; `properties` with the camera's reported `range`; `capture_outputs` |
| `properties` | - | the property allow-list (no camera needed) |
| `get` | `name` | `name`, `value` |
| `set` | `name`, `value` | `name`, `value` (read back where readable). Numbers outside the camera's reported range are refused |
| `start_stream` | `depth?: {format?, width?, height?, fps?}`, `rgb?: false or {...}` | the chosen `depth`/`rgb` modes, and `depth_scale`. Defaults are 3DViewer's: Z16 640x400 and RGB8 1280x800, else the first mode listed. RGB is on if the camera has it |
| `stop_stream` | - | `was_streaming` |
| `capture` | `dir` (absolute), `name`, `outputs?` [depth, ir, rgb, ply], `texture?`, `binary_ply?` (default true), `fresh?` (default true), `timeout_ms?` (default 5000) | `files` [{kind, path, bytes}], `skipped` [{kind, reason}], `points`, `trigger`, `depth_scale`, frame info, calibration, `warnings?` |
| `restart` | - | reboots the camera; you have to `connect` again |
| `shutdown` | - | stops, disconnects and exits |

### Properties

The names come from `propertymap.cpp`, which is also where each one's SDK ID and
conversion are defined. Run `properties` for the full list with types and
descriptions. In short:

- Basic: `depth.gain`, `depth.exposure` (us), `depth.frame_time`, `rgb.gain`,
  `rgb.auto_exposure`, `rgb.auto_white_balance`, `rgb.white_balance`.
- Extension, used by 3DViewer: `depth.scale` (read-only, mm per raw unit), `depth.range`
  `{min,max}` mm, `depth.roi` `{left,top,right,bottom}` %, `depth.auto_exposure_mode`,
  `depth.hdr_mode`, `depth.contrast_min`, `trigger_mode` (off/software/hardware),
  `rgb.exposure_time` (us).
- Extension, **declared in the SDK but never used by 3DViewer**, so behavior on the
  POP 2 is unknown (`used_by_3dviewer: false`): `cpu_temperature`, `gyro_supported`,
  `fast_scan_mode`, `multiframe_fusion`, `fringe_pattern`, `led`, `led_ctrl`
  `{led: ir|rgb|laser, mode: steady|blink|enable|disable}`, `laser.on`,
  `laser.brightness`.

### Capture details

- In `trigger_mode: software` the bridge calls `softTrigger()` and then waits for the
  frame. Otherwise it takes the next streamed frame.
- `fresh` (the default) first asks the SDK to clear its frame buffer
  (`PROPERTY_EXT_CLEAR_FRAME_BUFFER`), so the capture shows the scene as it is now, for
  example after a turntable move. If the SDK refuses, the reply says so in `warnings`.
  Whether this is enough on a POP 2 needs checking on hardware.
- Files use the same formats 3DViewer saves:
  - `<name>_depth.png`: 16-bit, raw units; multiply by `depth_scale` for mm.
  - `<name>_ir_left/right.png`: only with the `Z16Y8Y8` or `PAIR` depth formats.
  - `<name>_rgb.png`, or `.jpg` for MJPG.
  - `<name>.ply`: the SDK's point cloud in mm, with normals, plus color when
    `texture` is set.
- The PLY header is rewritten to use LF line endings, because on Windows the SDK writes
  it with CRLF.
- With `texture`, the SDK keeps only points that fall inside the RGB image, so a colored
  cloud can have fewer points than an uncolored one.
- An output this frame can't produce is listed in `skipped` with a reason; it isn't an
  error.
- The bridge validates `name` (no path separators) and requires an absolute `dir`. It
  does **not** confine `dir` to a root; the host (mhs2revo) must do that.

## Code

| File | What |
|------|------|
| `main.cpp` | stdio setup and the read-dispatch-write loop |
| `bridgeserver.*` | command handling and connection/stream state; no stdio, no SDK library |
| `propertymap.*` | the property allow-list and JSON <-> SDK conversions |
| `capturewriter.*` | writes the capture files |
| `camerabackend.h` | the interface to the camera |
| `sdkbackend.*` | that interface on the real SDK |

`src/tests/test_bridge.cpp` drives `BridgeServer` and `CaptureWriter` through a fake
backend, and `revo_bridge_selftest` runs the real exe. Neither of them needs a camera.
