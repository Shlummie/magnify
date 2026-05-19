# Center Magnifier

This utility is a native Windows C++ magnifier built on `Magnification.dll`.

## Controls

- Press the configured trigger button to control the magnifier. The default is the mouse back side button (`XBUTTON1`).
- Hold the configured wheel zoom modifier and scroll the mouse wheel to zoom the magnifier in or out. The default modifier is `ctrl`; wheel input is consumed while the modifier is held.
- Use the tray menu's `Center Dot` item to show or hide the green hipfire dot at the center of the primary monitor.
- In `Toggle` mode, each press shows or hides the magnifier.
- In `Hold To Show` mode, the magnifier is only visible while the button is held down.
- Left-click the tray icon to show or hide the magnifier.
- Right-click the tray icon to show or hide the center dot, change trigger mode, rebind the trigger button, rebind the wheel zoom modifier, switch between refresh-rate presets from `60 Hz` through `240 Hz`, pick zoom presets, pick size presets, enter custom zoom and custom size values, reload the config file, or quit cleanly.

## Run It

Double-click [Launch Center Magnifier.bat](Launch%20Center%20Magnifier.bat) or run:

```bat
CenterMagnifierNative.exe
```

## Build

Run [Build Center Magnifier Native.bat](Build%20Center%20Magnifier%20Native.bat) or use a Visual Studio Developer Command Prompt:

```bat
cl /nologo /std:c++17 /EHsc /W4 /permissive- /O2 center_magnifier_native.cpp /Fe:CenterMagnifierNative.exe
```

The native app and its config file live here:

```text
.\CenterMagnifierNative.exe
.\center_magnifier_native.ini
```

You can edit the config file directly and then use the tray menu's `Reload Config File` action. The editable keys are:

```ini
[Magnifier]
TargetFps=180
InputMode=toggle
WindowWidth=500
WindowHeight=500
ZoomFactor=2.00
TriggerButton=mouse_back
ZoomModifierButton=ctrl
CenterDotEnabled=true
CenterDotSize=7
```

`TriggerButton` accepts common names like `mouse_back`, `mouse_forward`, `middle_mouse`, `space`, `enter`, `shift`, `ctrl`, `alt`, single letters/numbers like `a` or `5`, function keys like `f8`, or a Windows virtual-key code like `vk_0x41`. The tray menu also has a `Trigger Button` submenu with common presets and a `Press a Key/Button...` capture option that can capture any keyboard key or mouse button.
`ZoomModifierButton` accepts the same values. The tray menu has a `Wheel Zoom Modifier` submenu with common presets and the same capture option.
`TargetFps` is clamped to the supported `5` through `240` FPS range.
`CenterDotEnabled` accepts `true` or `false`; `CenterDotSize` is clamped from `3` through `31` pixels.
