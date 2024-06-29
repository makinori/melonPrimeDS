<p align="center"><img src="./res/icon/melon_128x128.png"></p>
<h1 align="center"><b>melonPrimeDS</b></h1>
<br>

Modded version of [melonDS](https://melonds.kuribo64.net/) emulator to play Metroid Prime Hunters.

It's a bit of a hack but the goal is to make the game as fun as possible using mouse and keyboard.

I originally made this for controller but because there's no lock-on, it wasn't really fun to play.

**Please read the instructions carefully.**

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Q5Q0MLBI)

[melonPrimeDS.webm](https://github.com/makidoll/melonPrimeDS/assets/8362329/69ab26bb-7205-451a-a11c-70a2ca0b549d)

### Download

Releases for [Windows, Linux and macOS here!](https://github.com/makidoll/melonPrimeDS/releases)

[![aur](https://img.shields.io/aur/version/melonprimeds-bin?style=flat&logo=archlinux)](https://aur.archlinux.org/packages/melonprimeds-bin)


> **⚠️🖱️ Note if using mouse acceleration!**<br>
> Please disable mouse acceleration or it will feel strange.<br>
> Find a [guide for Windows here](https://www.lifewire.com/turn-off-mouse-acceleration-in-windows-11-5193828) and use [SteerMouse if on macOS](https://plentycom.jp/en/steermouse/index.html)

> **⚠️ Note for macOS users!**<br>
> Once you start the program, you're going to have to go into macOS settings:<br>
> **Privacy & Security > Accessibility**, and ensure melonPrimeDS is enabled.

### Instructions

-   Fyi. the emulator hack uses a different config path than melonDS, so this won't conflict

-   Aquire a copy of the **USA 1.1 rom** of Metroid Prime Hunters<br>
    Other regions don't work beacuse memory injection is used

-   Make sure to set all DS bindings to `None` in<br>
    `Config → Input and hotkeys → DS keypad`<br>
    Defaults should already be empty<br>
    _(click binding and press backspace)_

-   Find Metroid related `Keyboard mappings` in<br>
    `Metroid → Input settings`<br>
    Recommended defaults have already been set, but feel free to change them if you want to

    Notes:

    -   Focusing the window will capture your mouse. Use `ESC` to release.
    -   The stylus gets placed in the middle of the DS screen for aiming which can cause accidental presses
    -   Whilst holding the virtual stylus button `Tab`, use your mouse to click around on the touchscreen
    -   UI OK `F` will press "OK" on the touch screen, which will also jump and briefly break aiming
    -   UI left `Q` and right `E` will also press on the touch screen, for scan visor messages
    -   When in map view, press `Shift` to zoom out and `LMB` to zoom in

-   Find Metroid sensitivity settings in<br>
    `Metroid → Other settings`<br>
    <br>
    When in-game, **make sure to set the aim sensitivty to the lowest!**<br>
    The DS touchscreen isn't very precise, so setting it to lowest helps<br>
-   Also recommended to set audio settings in-game to headphones

<br>
<img src="./metroid/keyboard.png"/>

### Default settings changed from melonDS

-   Fullscreen toggle set to `F11`
-   Screen layout set to **horizontal**
-   Screen sizing set to **emphasize top**
-   Screen filter set to **false**
-   3D renderer set to **OpenGL**
-   3D scale factor set to **8x** for 1440p
-   JIT recompiler set to **enabled** _(helps with performance)_

VSync was already disabled but keeping it off also helps with performance

### Build

I modified melonDS and played Hunters on Linux. Building is straightforward

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -GNinja
ninja
```

Downloadable builds were made using GitHub actions

### Todo

-   Change controls when map is open. Need to find memory location
-   Update to latest melonDS
-   Would be really nice if weapon switching was done through the memory 
