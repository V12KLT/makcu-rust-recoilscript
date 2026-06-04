# How To Use

ForgeRecoil is a Windows desktop app that controls mouse movement through a
Makcu hardware device. The current build is Makcu-only and does not include a
remote access gate or the old software driver path.

## Requirements

- Windows 10 or Windows 11
- A connected Makcu device
- Visual Studio 2019, Visual Studio 2022, or newer C++ build tools
- DirectX 11 support

## Build

1. Open the project folder.
2. Run `build.bat`.
3. The compiled app will be created at `build/MakcuRecoil.exe`.

## Run

1. Plug in the Makcu device before launching the app.
2. Run `build/MakcuRecoil.exe`.
3. Click `LAUNCH` on the Makcu connection screen.
4. If the app asks you to close game processes, close them and wait for the
   loader to continue.

## Main Controls

- `Insert`: show or hide the menu by default.
- Recoil tab: choose weapon, scope, barrel, and recoil mode.
- Tuning tab: adjust sensitivity, field of view, smoothing, and movement speed.
- Settings tab: change theme, toggle visual effects, and configure main hotkeys.
- Keybinds tab: add custom keybinds or presets.

## Settings

Settings are saved next to the executable in `build/saved_settings.json`.
Deleting that file will reset the app to default settings on the next launch.

## Troubleshooting

- If the loader says the Makcu device was not found, reconnect the device and
  launch the app again.
- If the app opens an older-looking login screen, close every running
  `MakcuRecoil.exe` process and start the newly built executable from `build`.
- If the build fails, install Visual Studio C++ build tools and run `build.bat`
  again from the project folder.

Use this software only in environments where you have permission to use hardware
mouse automation.
