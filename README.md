<img src="./res/icon/melon_64x64.png"/>

# melonDS: Metroid Hunters

Modded version of melonDS emulator to play Metroid Hunters.

It's a bit of a hack but the goal is to make the game as fun as possible using a controller.

### Main instructions

-   Make sure to set all bindings to `None` in<br>
    `Config → Input and hotkeys → DS keypad`<br>
    _(click binding and press backspace)_

-   Set Metroid Hunters related `Joystick mappings` in<br>
    `Config → Input and hotkeys → Add-ons`<br>
    Recommended Nintendo layout controls have been added in parentheses.

    Notes:

    -   Left and Right trigger are the same button.
    -   UI OK (A) will press "OK" on the touch screen, which will also jump and briefly break aiming.<br>
        Just be mindful that the dedicated jump button (B) is what you should use.
    -   UI left and right will also press on the touch screen, for scan visor messages
    -   Cycle weapon (X) will try to select the 3rd weapon which you won't have yet when you start
    -   When in map view, press (Y) to zoom out and (LT/RT) to zoom in

    <br>
    <img src="./metroid/hunters%20controls.png" height="250"/>

### Optional instructions

-   Enable JIT to improve performance<br>
    `Config → Emu settings → CPU emulation → Enable JIT recompiler`

-   Render game at a high resolution<br>

    -   Disable `Config → Screen filtering`<br>
    -   `Config → Video settings`<br>
        Set `3D renderer` to `OpenGL`<br>
        Disable `VSync` for lower latency<br>
        Set `Internal resolution` to next highest for your monitor

-   My recommended screen layout<br>
    `Config → Screen layout → Horizontal`<br>
    `Config → Screen sizing → Emphasize top`<br>

-   Hide mouse after inactivity<br>
    `Config → Interface settings → Hide mouse after inactivity`

-   Bind `Toggle fullscreen` to `F` or something else<br>
    `Config → Input and hotkeys → General hotkeys`
