# SkyScript

SkyScript is an X-Plane 12 browser plugin that renders into a normal X-Plane floating window. In desktop mode it behaves like a movable, resizable native window. In VR it switches to an X-Plane VR window.

## Requirements

- X-Plane SDK 4.2.0 at `/Volumes/storage/git/SkyScript/SDK`
- CEF tree under `lib/mac_x64/cef`
- `lib/mac_x64/cef/Release/Chromium Embedded Framework.framework`
- `lib/mac_x64/cef/libcef_dll_wrapper.a`

Expected layout:

```txt
skyscript-cef/
├── lib
│   └── mac_x64
│       └── cef
│           ├── include
│           ├── Release
│           │   └── Chromium Embedded Framework.framework
│           └── libcef_dll_wrapper.a
└── ...
```

## Build

The project is XP12-only and builds `arm64` on macOS.

```sh
printf 'y\nmac\n12\nn\nn\n' | ./build_platforms.sh
```

Set a different SDK root if needed:

```sh
XPLANE_SDK_ROOT=/path/to/SDK ./build_platforms.sh
```

## Runtime IDs

- `skyscript/toggle`
- `skyscript/url`
- `skyscript/refresh`
- `skyscript/visible`
