/*
    Copyright 2016-2023 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <QImage>
#include <QPainter>
#include <QLabel>      
#include <QWidget>     
#include <optional>
#include <vector>
#include <string>
#include <algorithm>

#include <SDL2/SDL.h>

#include "main.h"
#include "Input.h"
#include "AudioInOut.h"

#include "types.h"
#include "version.h"

#include "FrontendUtil.h"

#include "Args.h"
#include "NDS.h"
#include "NDSCart.h"
#include "GBACart.h"
#include "GPU.h"
#include "SPU.h"
#include "Wifi.h"
#include "Platform.h"
#include "LocalMP.h"
#include "Config.h"
#include "RTC.h"
#include "DSi.h"
#include "DSi_I2C.h"
#include "GPU3D_Soft.h"
#include "GPU3D_OpenGL.h"

#include "Savestate.h"

#include "ROMManager.h"
//#include "ArchiveUtil.h"
//#include "CameraManager.h"

//#include "CLI.h"

// #include "RawInputThread.h"
#include "overlay_shaders.h"

#include "melonPrime/def.h"

// TODO: uniform variable spelling
using namespace melonDS;

// TEMP
extern bool RunningSomething;
extern MainWindow* mainWindow;
extern int autoScreenSizing;
extern int videoRenderer;
extern bool videoSettingsDirty;


float mouseX;
float mouseY;

EmuThread::EmuThread(QObject* parent) : QThread(parent)
{
    EmuStatus = emuStatus_Exit;
    EmuRunning = emuStatus_Paused;
    EmuPauseStack = EmuPauseStackRunning;
    RunningSomething = false;

    connect(this, SIGNAL(windowUpdate()), mainWindow->panel, SLOT(repaint()));
    connect(this, SIGNAL(windowTitleChange(QString)), mainWindow, SLOT(onTitleUpdate(QString)));
    connect(this, SIGNAL(windowEmuStart()), mainWindow, SLOT(onEmuStart()));
    connect(this, SIGNAL(windowEmuStop()), mainWindow, SLOT(onEmuStop()));
    connect(this, SIGNAL(windowEmuPause()), mainWindow->actPause, SLOT(trigger()));
    connect(this, SIGNAL(windowEmuReset()), mainWindow->actReset, SLOT(trigger()));
    connect(this, SIGNAL(windowEmuFrameStep()), mainWindow->actFrameStep, SLOT(trigger()));
    connect(this, SIGNAL(windowLimitFPSChange()), mainWindow->actLimitFramerate, SLOT(trigger()));
    connect(this, SIGNAL(screenLayoutChange()), mainWindow->panel, SLOT(onScreenLayoutChanged()));
    connect(this, SIGNAL(windowFullscreenToggle()), mainWindow, SLOT(onFullscreenToggled()));
    connect(this, SIGNAL(swapScreensToggle()), mainWindow->actScreenSwap, SLOT(trigger()));
    connect(this, SIGNAL(screenEmphasisToggle()), mainWindow, SLOT(onScreenEmphasisToggled()));
}

std::unique_ptr<NDS> EmuThread::CreateConsole(
    std::unique_ptr<melonDS::NDSCart::CartCommon>&& ndscart,
    std::unique_ptr<melonDS::GBACart::CartCommon>&& gbacart
) noexcept
{
    auto arm7bios = ROMManager::LoadARM7BIOS();
    if (!arm7bios)
        return nullptr;

    auto arm9bios = ROMManager::LoadARM9BIOS();
    if (!arm9bios)
        return nullptr;

    auto firmware = ROMManager::LoadFirmware(Config::ConsoleType);
    if (!firmware)
        return nullptr;

#ifdef JIT_ENABLED
    JITArgs jitargs {
        static_cast<unsigned>(Config::JIT_MaxBlockSize),
        Config::JIT_LiteralOptimisations,
        Config::JIT_BranchOptimisations,
        Config::JIT_FastMemory,
    };
#endif

#ifdef GDBSTUB_ENABLED
    GDBArgs gdbargs {
        static_cast<u16>(Config::GdbPortARM7),
        static_cast<u16>(Config::GdbPortARM9),
        Config::GdbARM7BreakOnStartup,
        Config::GdbARM9BreakOnStartup,
    };
#endif

    NDSArgs ndsargs {
        std::move(ndscart),
        std::move(gbacart),
        *arm9bios,
        *arm7bios,
        std::move(*firmware),
#ifdef JIT_ENABLED
        Config::JIT_Enable ? std::make_optional(jitargs) : std::nullopt,
#else
        std::nullopt,
#endif
        static_cast<AudioBitDepth>(Config::AudioBitDepth),
        static_cast<AudioInterpolation>(Config::AudioInterp),
#ifdef GDBSTUB_ENABLED
        Config::GdbEnabled ? std::make_optional(gdbargs) : std::nullopt,
#else
        std::nullopt,
#endif
    };

    if (Config::ConsoleType == 1)
    {
        auto arm7ibios = ROMManager::LoadDSiARM7BIOS();
        if (!arm7ibios)
            return nullptr;

        auto arm9ibios = ROMManager::LoadDSiARM9BIOS();
        if (!arm9ibios)
            return nullptr;

        auto nand = ROMManager::LoadNAND(*arm7ibios);
        if (!nand)
            return nullptr;

        auto sdcard = ROMManager::LoadDSiSDCard();
        DSiArgs args {
            std::move(ndsargs),
            *arm9ibios,
            *arm7ibios,
            std::move(*nand),
            std::move(sdcard),
            Config::DSiFullBIOSBoot,
        };

        args.GBAROM = nullptr;

        return std::make_unique<melonDS::DSi>(std::move(args));
    }

    return std::make_unique<melonDS::NDS>(std::move(ndsargs));
}

bool EmuThread::UpdateConsole(UpdateConsoleNDSArgs&& ndsargs, UpdateConsoleGBAArgs&& gbaargs) noexcept
{
    // Let's get the cart we want to use;
    // if we wnat to keep the cart, we'll eject it from the existing console first.
    std::unique_ptr<NDSCart::CartCommon> nextndscart;
    if (std::holds_alternative<Keep>(ndsargs))
    { // If we want to keep the existing cart (if any)...
        nextndscart = NDS ? NDS->EjectCart() : nullptr;
        ndsargs = {};
    }
    else if (const auto ptr = std::get_if<std::unique_ptr<NDSCart::CartCommon>>(&ndsargs))
    {
        nextndscart = std::move(*ptr);
        ndsargs = {};
    }

    if (auto* cartsd = dynamic_cast<NDSCart::CartSD*>(nextndscart.get()))
    {
        // LoadDLDISDCard will return nullopt if the SD card is disabled;
        // SetSDCard will accept nullopt, which means no SD card
        cartsd->SetSDCard(ROMManager::GetDLDISDCardArgs());
    }

    std::unique_ptr<GBACart::CartCommon> nextgbacart;
    if (std::holds_alternative<Keep>(gbaargs))
    {
        nextgbacart = NDS ? NDS->EjectGBACart() : nullptr;
    }
    else if (const auto ptr = std::get_if<std::unique_ptr<GBACart::CartCommon>>(&gbaargs))
    {
        nextgbacart = std::move(*ptr);
        gbaargs = {};
    }

    if (!NDS || NDS->ConsoleType != Config::ConsoleType)
    { // If we're switching between DS and DSi mode, or there's no console...
        // To ensure the destructor is called before a new one is created,
        // as the presence of global signal handlers still complicates things a bit
        NDS = nullptr;
        NDS::Current = nullptr;

        NDS = CreateConsole(std::move(nextndscart), std::move(nextgbacart));

        if (NDS == nullptr)
            return false;

        NDS->Reset();
        NDS::Current = NDS.get();

        return true;
    }

    auto arm9bios = ROMManager::LoadARM9BIOS();
    if (!arm9bios)
        return false;

    auto arm7bios = ROMManager::LoadARM7BIOS();
    if (!arm7bios)
        return false;

    auto firmware = ROMManager::LoadFirmware(NDS->ConsoleType);
    if (!firmware)
        return false;

    if (NDS->ConsoleType == 1)
    { // If the console we're updating is a DSi...
        DSi& dsi = static_cast<DSi&>(*NDS);

        auto arm9ibios = ROMManager::LoadDSiARM9BIOS();
        if (!arm9ibios)
            return false;

        auto arm7ibios = ROMManager::LoadDSiARM7BIOS();
        if (!arm7ibios)
            return false;

        auto nandimage = ROMManager::LoadNAND(*arm7ibios);
        if (!nandimage)
            return false;

        auto dsisdcard = ROMManager::LoadDSiSDCard();

        dsi.SetFullBIOSBoot(Config::DSiFullBIOSBoot);
        dsi.ARM7iBIOS = *arm7ibios;
        dsi.ARM9iBIOS = *arm9ibios;
        dsi.SetNAND(std::move(*nandimage));
        dsi.SetSDCard(std::move(dsisdcard));
        // We're moving the optional, not the card
        // (inserting std::nullopt here is okay, it means no card)

        dsi.EjectGBACart();
    }

    if (NDS->ConsoleType == 0)
    {
        NDS->SetGBACart(std::move(nextgbacart));
    }

#ifdef JIT_ENABLED
    JITArgs jitargs {
        static_cast<unsigned>(Config::JIT_MaxBlockSize),
        Config::JIT_LiteralOptimisations,
        Config::JIT_BranchOptimisations,
        Config::JIT_FastMemory,
    };
    NDS->SetJITArgs(Config::JIT_Enable ? std::make_optional(jitargs) : std::nullopt);
#endif
    NDS->SetARM7BIOS(*arm7bios);
    NDS->SetARM9BIOS(*arm9bios);
    NDS->SetFirmware(std::move(*firmware));
    NDS->SetNDSCart(std::move(nextndscart));
    NDS->SPU.SetInterpolation(static_cast<AudioInterpolation>(Config::AudioInterp));
    NDS->SPU.SetDegrade10Bit(static_cast<AudioBitDepth>(Config::AudioBitDepth));

    NDS::Current = NDS.get();

    return true;
}

// CalculatePlayerAddress Function
uint32_t calculatePlayerAddress(uint32_t baseAddress, uint8_t playerPosition, int32_t increment) {
    // If player position is 0, return the base address without modification
    if (playerPosition == 0) {
        return baseAddress;
    }

    // Calculate using 64-bit integers to prevent overflow
    // Use playerPosition as is (no subtraction)
    int64_t result = static_cast<int64_t>(baseAddress) + (static_cast<int64_t>(playerPosition) * increment);

    // Ensure the result is within the 32-bit range
    if (result < 0 || result > UINT32_MAX) {
        return baseAddress;  // Return the original address if out of range
    }

    return static_cast<uint32_t>(result);
}

melonDS::u32 baseIsAltFormAddr;
melonDS::u32 baseLoadedSpecialWeaponAddr;
melonDS::u32 baseWeaponChangeAddr;
melonDS::u32 baseSelectedWeaponAddr;
melonDS::u32 baseChosenHunterAddr;
melonDS::u32 baseJumpFlagAddr;
melonDS::u32 inGameAddr;
melonDS::u32 PlayerPosAddr;
melonDS::u32 inVisorOrMapAddr;
melonDS::u32 baseAimXAddr;
melonDS::u32 baseAimYAddr;
melonDS::u32 aimXAddr;
melonDS::u32 aimYAddr;
melonDS::u32 isInAdventureAddr;
melonDS::u32 isMapOrUserActionPausedAddr; // for issue in AdventureMode, Aim Stopping when SwitchingWeapon. 

bool isAltForm;


void detectRomAndSetAddresses() {
    switch (globalChecksum) {
    case RomVersions::USA1_1:
        // USA1.1

        baseChosenHunterAddr = 0x020CBDA4; // BattleConfig:ChosenHunter 0 samus 1 kanden 2 trace 3 sylux 4 noxus 5 spire 6 weavel
        inGameAddr = 0x020eec40 + 0x8F0; // inGame:1
        inVisorOrMapAddr = 0x020D9A7D; // 推定アドレス
        PlayerPosAddr = 0x020DA538;
        baseIsAltFormAddr = 0x020DB098; // 1p(host)
        baseLoadedSpecialWeaponAddr = baseIsAltFormAddr + 0x56; // 1p(host). For special weapons only. Missile and powerBeam are not special weapon.
        baseWeaponChangeAddr = 0x020DB45B; // 1p(host)
        baseSelectedWeaponAddr = 0x020DB463; // 1p(host)
        baseJumpFlagAddr = baseSelectedWeaponAddr - 0xA;
        baseAimXAddr = 0x020DEDA6;
        baseAimYAddr = 0x020DEDAE;
        isInAdventureAddr = 0x020E83BC; // Read8 0x02: ADV, 0x03: Multi
        isMapOrUserActionPausedAddr = 0x020FBF18; // 0x00000001: true, 0x00000000 false. Read8 is enough though.
        isRomDetected = true;
        mainWindow->osdAddMessage(0, "Rom detected: US1.1");

        break;

    case RomVersions::USA1_0:
        // USA1.0
        baseChosenHunterAddr = 0x020CB51C; // BattleConfig:ChosenHunter
        inGameAddr = 0x020ee180 + 0x8F0; // inGame:1
        PlayerPosAddr = 0x020D9CB8;
        inVisorOrMapAddr = PlayerPosAddr - 0xabb; // 推定アドレス
        baseIsAltFormAddr = 0x020DC6D8 - 0x1EC0; // 1p(host)
        baseLoadedSpecialWeaponAddr = baseIsAltFormAddr + 0x56; // 1p(host). For special weapons only. Missile and powerBeam are not special weapon.
        baseWeaponChangeAddr = 0x020DCA9B - 0x1EC0; // 1p(host)
        baseSelectedWeaponAddr = 0x020DCAA3 - 0x1EC0; // 1p(host)
        baseJumpFlagAddr = baseSelectedWeaponAddr - 0xA;
        baseAimXAddr = 0x020de526;
        baseAimYAddr = 0x020de52E;
        isInAdventureAddr = 0x020E78FC; // Read8 0x02: ADV, 0x03: Multi
        isMapOrUserActionPausedAddr = 0x020FB458; // 0x00000001: true, 0x00000000 false. Read8 is enough though.
        isRomDetected = true;
        mainWindow->osdAddMessage(0, "Rom detected: US1.0");

        break;

    case RomVersions::JAPAN1_0:
        // Japan1.0
        baseChosenHunterAddr = 0x020CD358; // BattleConfig:ChosenHunter
        inGameAddr = 0x020F0BB0; // inGame:1
        PlayerPosAddr = 0x020DBB78;
        inVisorOrMapAddr = PlayerPosAddr - 0xabb; // 推定アドレス
        baseIsAltFormAddr = 0x020DC6D8; // 1p(host)
        baseLoadedSpecialWeaponAddr = baseIsAltFormAddr + 0x56; // 1p(host). For special weapons only. Missile and powerBeam are not special weapon.
        baseWeaponChangeAddr = 0x020DCA9B; // 1p(host)
        baseSelectedWeaponAddr = 0x020DCAA3; // 1p(host)
        baseJumpFlagAddr = baseSelectedWeaponAddr - 0xA;
        baseAimXAddr = 0x020E03E6;
        baseAimYAddr = 0x020E03EE;
        isInAdventureAddr = 0x020E9A3C; // Read8 0x02: ADV, 0x03: Multi
        isMapOrUserActionPausedAddr = 0x020FD598; // 0x00000001: true, 0x00000000 false. Read8 is enough though.
        isRomDetected = true;
        mainWindow->osdAddMessage(0, "Rom detected: JP1.0");

        break;

    case RomVersions::JAPAN1_1:
        // Japan1.1
        baseChosenHunterAddr = 0x020CD318; // BattleConfig:ChosenHunter
        inGameAddr = 0x020F0280 + 0x8F0; // inGame:1
        PlayerPosAddr = 0x020DBB38;
        inVisorOrMapAddr = PlayerPosAddr - 0xabb; // 推定アドレス
        baseIsAltFormAddr = 0x020DC6D8 - 0x64; // 1p(host)
        baseLoadedSpecialWeaponAddr = baseIsAltFormAddr + 0x56; // 1p(host). For special weapons only. Missile and powerBeam are not special weapon.
        baseWeaponChangeAddr = 0x020DCA9B - 0x40; // 1p(host)
        baseSelectedWeaponAddr = 0x020DCAA3 - 0x40; // 1p(host)
        baseJumpFlagAddr = baseSelectedWeaponAddr - 0xA;
        baseAimXAddr = 0x020e03a6;
        baseAimYAddr = 0x020e03ae;
        isInAdventureAddr = 0x020E99FC; // Read8 0x02: ADV, 0x03: Multi
        isMapOrUserActionPausedAddr = 0x020FD558; // 0x00000001: true, 0x00000000 false. Read8 is enough though.
        isRomDetected = true;
        mainWindow->osdAddMessage(0, "Rom detected: JP1.1");

        break;

    case RomVersions::EU1_0:
        // EU1.0
        baseChosenHunterAddr = 0x020CBDC4; // BattleConfig:ChosenHunter
        inGameAddr = 0x020eec60 + 0x8F0; // inGame:1
        PlayerPosAddr = 0x020DA558;
        inVisorOrMapAddr = PlayerPosAddr - 0xabb; // 推定アドレス
        baseIsAltFormAddr = 0x020DC6D8 - 0x1620; // 1p(host)
        baseLoadedSpecialWeaponAddr = baseIsAltFormAddr + 0x56; // 1p(host). For special weapons only. Missile and powerBeam are not special weapon.
        baseWeaponChangeAddr = 0x020DCA9B - 0x1620; // 1p(host)
        baseSelectedWeaponAddr = 0x020DCAA3 - 0x1620; // 1p(host)
        baseJumpFlagAddr = baseSelectedWeaponAddr - 0xA;
        baseAimXAddr = 0x020dedc6;
        baseAimYAddr = 0x020dedcE;
        isInAdventureAddr = 0x020E83DC; // Read8 0x02: ADV, 0x03: Multi
        isMapOrUserActionPausedAddr = 0x020FBF38; // 0x00000001: true, 0x00000000 false. Read8 is enough though.
        isRomDetected = true;
        mainWindow->osdAddMessage(0, "Rom detected: EU1.0");

        break;

    case RomVersions::EU1_1:
        // EU1.1
        baseChosenHunterAddr = 0x020CBE44; // BattleConfig:ChosenHunter
        inGameAddr = 0x020eece0 + 0x8F0; // inGame:1
        PlayerPosAddr = 0x020DA5D8;
        inVisorOrMapAddr = PlayerPosAddr - 0xabb; // 推定アドレス
        baseIsAltFormAddr = 0x020DC6D8 - 0x15A0; // 1p(host)
        baseLoadedSpecialWeaponAddr = baseIsAltFormAddr + 0x56; // 1p(host). For special weapons only. Missile and powerBeam are not special weapon.
        baseWeaponChangeAddr = 0x020DCA9B - 0x15A0; // 1p(host)
        baseSelectedWeaponAddr = 0x020DCAA3 - 0x15A0; // 1p(host)
        baseJumpFlagAddr = baseSelectedWeaponAddr - 0xA;
        baseAimXAddr = 0x020dee46;
        baseAimYAddr = 0x020dee4e;
        isInAdventureAddr = 0x020E845C; // Read8 0x02: ADV, 0x03: Multi
        isMapOrUserActionPausedAddr = 0x020FBFB8; // 0x00000001: true, 0x00000000 false. Read8 is enough though.
        mainWindow->osdAddMessage(0, "Rom detected: EU1.1");

        isRomDetected = true;

        break;

    case RomVersions::KOREA1_0:
        // Korea1.0
        baseChosenHunterAddr = 0x020C4B88; // BattleConfig:ChosenHunter
        inGameAddr = 0x020E81B4; // inGame:1
        inVisorOrMapAddr = PlayerPosAddr - 0xabb; // 推定アドレス
        PlayerPosAddr = 0x020D33A9; // it's weird but "3A9" is correct.
        baseIsAltFormAddr = 0x020DC6D8 - 0x87F4; // 1p(host)
        baseLoadedSpecialWeaponAddr = baseIsAltFormAddr + 0x56; // 1p(host). For special weapons only. Missile and powerBeam are not special weapon.
        baseWeaponChangeAddr = 0x020DCA9B - 0x87F4; // 1p(host)
        baseSelectedWeaponAddr = 0x020DCAA3 - 0x87F4; // 1p(host)
        baseJumpFlagAddr = baseSelectedWeaponAddr - 0xA;
        baseAimXAddr = 0x020D7C0E;
        baseAimYAddr = 0x020D7C16;
        isInAdventureAddr = 0x020E11F8; // Read8 0x02: ADV, 0x03: Multi
        isMapOrUserActionPausedAddr = 0x020F4CF8; // 0x00000001: true, 0x00000000 false. Read8 is enough though.
        mainWindow->osdAddMessage(0, "Rom detected: KR1.0");

        isRomDetected = true;

        break;

    default:
        // 未対応のチェックサムに対する処理
        // デフォルトの動作やエラーメッセージの追加
        break;
    }
}


void EmuThread::run()
{
    u32 mainScreenPos[3];
    Platform::FileHandle* file;

    UpdateConsole(nullptr, nullptr);
    // No carts are inserted when melonDS first boots

    mainScreenPos[0] = 0;
    mainScreenPos[1] = 0;
    mainScreenPos[2] = 0;
    autoScreenSizing = 0;

    videoSettingsDirty = false;

    if (mainWindow->hasOGL)
    {
        screenGL = static_cast<ScreenPanelGL*>(mainWindow->panel);
        screenGL->initOpenGL();
        videoRenderer = Config::_3DRenderer;
    }
    else
    {
        screenGL = nullptr;
        videoRenderer = 0;
    }

    if (videoRenderer == 0)
    { // If we're using the software renderer...
        NDS->GPU.SetRenderer3D(std::make_unique<SoftRenderer>(Config::Threaded3D != 0));
    }
    else
    {
        auto glrenderer =  melonDS::GLRenderer::New();
        glrenderer->SetRenderSettings(Config::GL_BetterPolygons, Config::GL_ScaleFactor);
        NDS->GPU.SetRenderer3D(std::move(glrenderer));
    }

    Input::Init();

    u32 nframes = 0;
    double perfCountsSec = 1.0 / SDL_GetPerformanceFrequency();
    double lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
    double frameLimitError = 0.0;
    double lastMeasureTime = lastTime;

    u32 winUpdateCount = 0, winUpdateFreq = 1;
    u8 dsiVolumeLevel = 0x1F;

    file = Platform::OpenLocalFile("rtc.bin", Platform::FileMode::Read);
    if (file)
    {
        RTC::StateData state;
        Platform::FileRead(&state, sizeof(state), 1, file);
        Platform::CloseFile(file);
        NDS->RTC.SetState(state);
    }

    char melontitle[100];




    auto frameAdvanceOnce {
    [&]()
    {
        Input::Process();

        if (Input::HotkeyPressed(HK_FastForwardToggle)) emit windowLimitFPSChange();

        if (Input::HotkeyPressed(HK_Pause)) emit windowEmuPause();
        if (Input::HotkeyPressed(HK_Reset)) emit windowEmuReset();
        if (Input::HotkeyPressed(HK_FrameStep)) emit windowEmuFrameStep();

        if (Input::HotkeyPressed(HK_FullscreenToggle)) emit windowFullscreenToggle();

        if (Input::HotkeyPressed(HK_SwapScreens)) emit swapScreensToggle();
        if (Input::HotkeyPressed(HK_SwapScreenEmphasis)) emit screenEmphasisToggle();

        // Lambda to update aim sensitivity and display a message
        auto updateAimSensitivity = [](int change) {
            // Store the current sensitivity in a local variable
            int currentSensitivity = Config::MetroidAimSensitivity;

            // Calculate the new sensitivity
            int newSensitivity = currentSensitivity + change;

            // Check if the new sensitivity is at least 1
            if (newSensitivity >= 1) {
                // Update the config only if the value has changed
                if (newSensitivity != currentSensitivity) {
                    Config::MetroidAimSensitivity = newSensitivity;
                    // Save the changes to the configuration file (to persist settings for future sessions)
                    Config::Save();
                }
                // Create and display the OSD message
                mainWindow->osdAddMessage(0, ("AimSensi Updated: " + std::to_string(newSensitivity)).c_str());
            }
            else {
                // Display a message when trying to decrease below 1
                mainWindow->osdAddMessage(0, "AimSensi cannot be decreased below 1");
            }
            };

        // Sensitivity UP
        if (Input::HotkeyReleased(HK_MetroidIngameSensiUp)) {
            updateAimSensitivity(1);  // Increase sensitivity by 1
        }

        // Sensitivity DOWN
        if (Input::HotkeyReleased(HK_MetroidIngameSensiDown)) {
            updateAimSensitivity(-1);  // Decrease sensitivity by 1
        }


        if (EmuRunning == emuStatus_Running || EmuRunning == emuStatus_FrameStep)
        {
            EmuStatus = emuStatus_Running;
            if (EmuRunning == emuStatus_FrameStep) EmuRunning = emuStatus_Paused;

            // if (Input::HotkeyPressed(HK_SolarSensorDecrease))
            // {
            //     int level = NDS->GBACartSlot.SetInput(GBACart::Input_SolarSensorDown, true);
            //     if (level != -1)
            //     {
            //         mainWindow->osdAddMessage(0, "Solar sensor level: %d", level);
            //     }
            // }
            // if (Input::HotkeyPressed(HK_SolarSensorIncrease))
            // {
            //     int level = NDS->GBACartSlot.SetInput(GBACart::Input_SolarSensorUp, true);
            //     if (level != -1)
            //     {
            //         mainWindow->osdAddMessage(0, "Solar sensor level: %d", level);
            //     }
            // }

            if (NDS->ConsoleType == 1)
            {
                DSi& dsi = static_cast<DSi&>(*NDS);
                double currentTime = SDL_GetPerformanceCounter() * perfCountsSec;

                // Handle power button
                if (Input::HotkeyDown(HK_PowerButton))
                {
                    dsi.I2C.GetBPTWL()->SetPowerButtonHeld(currentTime);
                }
                else if (Input::HotkeyReleased(HK_PowerButton))
                {
                    dsi.I2C.GetBPTWL()->SetPowerButtonReleased(currentTime);
                }

                // Handle volume buttons
                if (Input::HotkeyDown(HK_VolumeUp))
                {
                    dsi.I2C.GetBPTWL()->SetVolumeSwitchHeld(DSi_BPTWL::volumeKey_Up);
                }
                else if (Input::HotkeyReleased(HK_VolumeUp))
                {
                    dsi.I2C.GetBPTWL()->SetVolumeSwitchReleased(DSi_BPTWL::volumeKey_Up);
                }

                if (Input::HotkeyDown(HK_VolumeDown))
                {
                    dsi.I2C.GetBPTWL()->SetVolumeSwitchHeld(DSi_BPTWL::volumeKey_Down);
                }
                else if (Input::HotkeyReleased(HK_VolumeDown))
                {
                    dsi.I2C.GetBPTWL()->SetVolumeSwitchReleased(DSi_BPTWL::volumeKey_Down);
                }

                dsi.I2C.GetBPTWL()->ProcessVolumeSwitchInput(currentTime);
            }

            // update render settings if needed
            // HACK:
            // once the fast forward hotkey is released, we need to update vsync
            // to the old setting again
            if (videoSettingsDirty || Input::HotkeyReleased(HK_FastForward))
            {
                if (screenGL)
                {
                    screenGL->setSwapInterval(Config::ScreenVSync ? Config::ScreenVSyncInterval : 0);
                    videoRenderer = Config::_3DRenderer;
                }
#ifdef OGLRENDERER_ENABLED
                else
#endif
                {
                    videoRenderer = 0;
                }

                videoRenderer = screenGL ? Config::_3DRenderer : 0;

                videoSettingsDirty = false;

                if (videoRenderer == 0)
                { // If we're using the software renderer...
                    NDS->GPU.SetRenderer3D(std::make_unique<SoftRenderer>(Config::Threaded3D != 0));
                }
                else
                {
                    auto glrenderer =  melonDS::GLRenderer::New();
                    glrenderer->SetRenderSettings(Config::GL_BetterPolygons, Config::GL_ScaleFactor);
                    NDS->GPU.SetRenderer3D(std::move(glrenderer));
                }
            }

            // process input and hotkeys
            // NDS->SetKeyMask(Input::InputMask); // doing this in metroid code

            // for some reason this is running when metroid menu is pressed
            // if (Input::HotkeyPressed(HK_Lid))
            // {
            //     bool lid = !NDS->IsLidClosed();
            //     NDS->SetLidClosed(lid);
            //     mainWindow->osdAddMessage(0, lid ? "Lid closed" : "Lid opened");
            // }

            // microphone input
            AudioInOut::MicProcess(*NDS);

            // auto screen layout
            if (Config::ScreenSizing == Frontend::screenSizing_Auto)
            {
                mainScreenPos[2] = mainScreenPos[1];
                mainScreenPos[1] = mainScreenPos[0];
                mainScreenPos[0] = NDS->PowerControl9 >> 15;

                int guess;
                if (mainScreenPos[0] == mainScreenPos[2] &&
                    mainScreenPos[0] != mainScreenPos[1])
                {
                    // constant flickering, likely displaying 3D on both screens
                    // TODO: when both screens are used for 2D only...???
                    guess = Frontend::screenSizing_Even;
                }
                else
                {
                    if (mainScreenPos[0] == 1)
                        guess = Frontend::screenSizing_EmphTop;
                    else
                        guess = Frontend::screenSizing_EmphBot;
                }

                if (guess != autoScreenSizing)
                {
                    autoScreenSizing = guess;
                    emit screenLayoutChange();
                }
            }


            // emulate
            u32 nlines = NDS->RunFrame();

            if (ROMManager::NDSSave)
                ROMManager::NDSSave->CheckFlush();

            if (ROMManager::GBASave)
                ROMManager::GBASave->CheckFlush();

            if (ROMManager::FirmwareSave)
                ROMManager::FirmwareSave->CheckFlush();

            if (!screenGL)
            {
                FrontBufferLock.lock();
                FrontBuffer = NDS->GPU.FrontBuffer;
                FrontBufferLock.unlock();
            }
            else
            {
                FrontBuffer = NDS->GPU.FrontBuffer;
                screenGL->drawScreenGL();
            }

#ifdef MELONCAP
            MelonCap::Update();
#endif // MELONCAP

            if (EmuRunning == emuStatus_Exit) return;

            winUpdateCount++;
            if (winUpdateCount >= winUpdateFreq && !screenGL)
            {
                emit windowUpdate();
                winUpdateCount = 0;
            }

            bool fastforward = Input::HotkeyDown(HK_FastForward);

            if (fastforward && screenGL && Config::ScreenVSync)
            {
                screenGL->setSwapInterval(0);
            }

            if (Config::DSiVolumeSync && NDS->ConsoleType == 1)
            {
                DSi& dsi = static_cast<DSi&>(*NDS);
                u8 volumeLevel = dsi.I2C.GetBPTWL()->GetVolumeLevel();
                if (volumeLevel != dsiVolumeLevel)
                {
                    dsiVolumeLevel = volumeLevel;
                    emit syncVolumeLevel();
                }

                Config::AudioVolume = volumeLevel * (256.0 / 31.0);
            }

            if (Config::AudioSync && !fastforward)
                AudioInOut::AudioSync(*this->NDS);

            double frametimeStep = nlines / (60.0 * 263.0);

            {
                bool limitfps = Config::LimitFPS && !fastforward;

                double practicalFramelimit = limitfps ? frametimeStep : 1.0 / 1000.0;

                double curtime = SDL_GetPerformanceCounter() * perfCountsSec;

                frameLimitError += practicalFramelimit - (curtime - lastTime);
                if (frameLimitError < -practicalFramelimit)
                    frameLimitError = -practicalFramelimit;
                if (frameLimitError > practicalFramelimit)
                    frameLimitError = practicalFramelimit;

                if (round(frameLimitError * 1000.0) > 0.0)
                {
                    SDL_Delay(round(frameLimitError * 1000.0));
                    double timeBeforeSleep = curtime;
                    curtime = SDL_GetPerformanceCounter() * perfCountsSec;
                    frameLimitError -= curtime - timeBeforeSleep;
                }

                lastTime = curtime;
            }

            nframes++;
            if (nframes >= 30)
            {
                double time = SDL_GetPerformanceCounter() * perfCountsSec;
                double dt = time - lastMeasureTime;
                lastMeasureTime = time;

                u32 fps = round(nframes / dt);
                nframes = 0;

                float fpstarget = 1.0/frametimeStep;

                winUpdateFreq = fps / (u32)round(fpstarget);
                if (winUpdateFreq < 1)
                    winUpdateFreq = 1;

                int inst = Platform::InstanceID();
                if (inst == 0)
                    sprintf(melontitle, "[%d/%.0f] melonPrimeDS " MELONPRIMEDS_VERSION " (" MELONDS_VERSION ")", fps, fpstarget);
                else
                    sprintf(melontitle, "[%d/%.0f] melonPrimeDS " MELONPRIMEDS_VERSION " (" MELONDS_VERSION ") (%d)", fps, fpstarget, inst+1);
                changeWindowTitle(melontitle);
            }
        }
        else
        {
            // paused
            nframes = 0;
            lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
            lastMeasureTime = lastTime;

            emit windowUpdate();

            EmuStatus = EmuRunning;

            int inst = Platform::InstanceID();
            if (inst == 0)
                sprintf(melontitle, "melonPrimeDS " MELONPRIMEDS_VERSION " (" MELONDS_VERSION ")");
            else
                sprintf(melontitle, "melonPrimeDS " MELONPRIMEDS_VERSION " (" MELONDS_VERSION ") (%d)", inst+1);
            changeWindowTitle(melontitle);

            SDL_Delay(75);

            if (screenGL)
                screenGL->drawScreenGL();

            ContextRequestKind contextRequest = ContextRequest;
            if (contextRequest == contextRequest_InitGL)
            {
                screenGL = static_cast<ScreenPanelGL*>(mainWindow->panel);
                screenGL->initOpenGL();
                ContextRequest = contextRequest_None;
            }
            else if (contextRequest == contextRequest_DeInitGL)
            {
                screenGL->deinitOpenGL();
                screenGL = nullptr;
                ContextRequest = contextRequest_None;
            }
        }
    }
    };

    auto frameAdvance {
    [&](int n)
    {
        for (int i = 0; i < n; i++) frameAdvanceOnce();
    }
    };

    // metroid prime hunters code
    // adapted from https://forums.desmume.org/viewtopic.php?id=11715

    // #define INTERP_IN(t) (t * t)
    // #define INTERP_IN_CUBIC(t) (t * t * t)
    // #define INTERP_IN_QUART(t) (t * t * t * t)

    #define INPUT_A 0
    #define INPUT_B 1
    #define INPUT_SELECT 2
    #define INPUT_START 3
    #define INPUT_RIGHT 4
    #define INPUT_LEFT 5
    #define INPUT_UP 6
    #define INPUT_DOWN 7
    #define INPUT_R 8
    #define INPUT_L 9
    #define INPUT_X 10
    #define INPUT_Y 11
    #define FN_INPUT_PRESS(i) Input::InputMask.setBit(i, false);
    #define FN_INPUT_RELEASE(i) Input::InputMask.setBit(i, true);





// #define ENABLE_MEMORY_DUMP 1
#ifdef ENABLE_MEMORY_DUMP
    int memoryDump = 0;
#endif


    bool enableAim = true;
    bool wasLastFrameFocused = false;

    float virtualStylusX = 128;
    float virtualStylusY = 96;

    const float dsAspectRatio = 256.0 / 192.0;
    const float aimAspectRatio = 6.0 / 4.0; // i have no idea

    // RawInputThread* rawInputThread = new RawInputThread(parent());
    // rawInputThread->start();

    auto processMoveInput = []() {
        const struct {
            int hotkey;
            int input;
        } moves[] = {
            {HK_MetroidMoveForward, INPUT_UP},
            {HK_MetroidMoveBack, INPUT_DOWN},
            {HK_MetroidMoveLeft, INPUT_LEFT},
            {HK_MetroidMoveRight, INPUT_RIGHT}
        };

        for (const auto& move : moves) {
            if (Input::HotkeyDown(move.hotkey)) {
                FN_INPUT_PRESS(move.input);
            } else {
                FN_INPUT_RELEASE(move.input);
            }
        }
    };

    
    bool drawVCur;

    uint8_t playerPosition;
    const int32_t playerAddressIncrement = 0xF30;
    const int32_t aimAddrIncrement = 0x48;
    uint32_t isAltFormAddr;
    uint32_t loadedSpecialWeaponAddr;
    uint32_t chosenHunterAddr;
    uint32_t weaponChangeAddr;
    uint32_t selectedWeaponAddr;
    uint32_t jumpFlagAddr;

    uint32_t boostGaugeAddr;
    uint32_t isBoostingAddr;


    bool isAddressCalculationNeeded;
    bool isInGame;
    bool isInAdventure;
    bool isSamus;

    while (EmuRunning != emuStatus_Exit) {



        auto isFocused = mainWindow->panel->getFocused();

        // auto mouseRel = rawInputThread->fetchMouseDelta();

        // The QPoint class defines a point in the plane using integer precision. 
        QPoint mouseRel;

        // 感度係数を定数として定義
        const float SENSITIVITY_FACTOR = Config::MetroidAimSensitivity * 0.01f;
        const float SENSITIVITY_FACTOR_VIRTUAL_STYLUS = Config::MetroidVirtualStylusSensitivity * 0.01f;

        // Calculate for aim 
        // updateMouseRelativeAndRecenterCursor

        // Handle the case when the window is focused
        if (isFocused) {
            // Get the center coordinates of the window
            auto windowCenter = mainWindow->geometry().center();

            // If the window was also focused in the previous frame
            if (wasLastFrameFocused) {
                // Calculate the relative mouse position (current cursor position - window center)
                mouseRel = QCursor::pos() - windowCenter;
            }

            // Move the cursor to the center of the window
            QCursor::setPos(windowCenter);
        }



        drawVCur = false;

        #ifdef ENABLE_MEMORY_DUMP
            if (Input::HotkeyPressed(HK_MetroidUIOk)) {
                printf("MainRAMMask 0x%.8" PRIXPTR "\n", (uintptr_t)NDS->MainRAMMask);
                QFile file("memory" + QString::number(memoryDump++) + ".bin");
                if (file.open(QIODevice::ReadWrite)) {
                    file.write(QByteArray((char*)NDS->MainRAM, NDS->MainRAMMaxSize));
                }
            }
        #endif

        if (!isRomDetected) {
            detectRomAndSetAddresses();
            isNewRom = false;
        }

        isInGame = NDS->ARM9Read16(inGameAddr) == 0x0001;

        // Auto Enable/Disable VirtualStylus Before/After the game
        // you can still enable VirtualStylus in Game
        if (!isInGame && !isVirtualStylusEnabled && ingameSoVirtualStylusAutolyDisabled) {
            isVirtualStylusEnabled = true;
            // mainWindow->osdAddMessage(0, "Virtual Stylus enabled");
            ingameSoVirtualStylusAutolyDisabled = false;
        }

        isAddressCalculationNeeded = false;

        if(isInGame && isVirtualStylusEnabled && !ingameSoVirtualStylusAutolyDisabled) {
            isVirtualStylusEnabled = false;
            // mainWindow->osdAddMessage(0, "Virtual Stylus disabled");
            ingameSoVirtualStylusAutolyDisabled = true;

            // inGame so need calculate address
            isAddressCalculationNeeded = true;
        }

        // VirtualStylus is Enabled when not in game
        isVirtualStylusEnabled = !isInGame;


        if (isAddressCalculationNeeded) {
            // Read once at game start

            // Read the player position
            playerPosition = NDS->ARM9Read8(PlayerPosAddr);

            // get addresses
            isAltFormAddr = calculatePlayerAddress(baseIsAltFormAddr, playerPosition, playerAddressIncrement);
            loadedSpecialWeaponAddr = calculatePlayerAddress(baseLoadedSpecialWeaponAddr, playerPosition, playerAddressIncrement);
            chosenHunterAddr = calculatePlayerAddress(baseChosenHunterAddr, playerPosition, 0x01);
            weaponChangeAddr = calculatePlayerAddress(baseWeaponChangeAddr, playerPosition, playerAddressIncrement);
            selectedWeaponAddr = calculatePlayerAddress(baseSelectedWeaponAddr, playerPosition, playerAddressIncrement);
            jumpFlagAddr = calculatePlayerAddress(baseJumpFlagAddr, playerPosition, playerAddressIncrement);

            // getChosenHunterAddr
            chosenHunterAddr = calculatePlayerAddress(baseChosenHunterAddr, playerPosition, 0x01);
            isSamus = NDS->ARM9Read8(chosenHunterAddr) == 0x00;

            boostGaugeAddr = isAltFormAddr + 0x44;
            isBoostingAddr = isAltFormAddr + 0x46;

            // aim addresses
            aimXAddr = calculatePlayerAddress(baseAimXAddr, playerPosition, aimAddrIncrement);
            aimYAddr = calculatePlayerAddress(baseAimYAddr, playerPosition, aimAddrIncrement);


            isInAdventure = NDS->ARM9Read8(isInAdventureAddr) == 0x02;

            // mainWindow->osdAddMessage(0, "Completed address calculation.");

		}

        if (isFocused) {


			if (!isVirtualStylusEnabled) {
                // inGame


                drawVCur = false;

                // Aiming

                /*
                // Lambda function to adjust scaled mouse input
                auto adjustMouseInput = [](float value) {
                    // For positive values less than 1, set to 1
                    if (value > 0 && value < 1.0f) {
                        return 1.0f;
                    }
                    // For negative values greater than -1, set to -1
                    else if (value < 0 && value > -1.0f) {
                        return -1.0f;
                    }
                    // For other values, return as is
                    return value;
                    };
                */

                // Lambda function to adjust scaled mouse input
                auto adjustMouseInput = [](float value) {
                    // For positive values between 0.5 and 1, set to 1
                    if (value >= 0.5f && value < 1.0f) {
                        return 1.0f;
                    }
                    // For negative values between -0.5 and -1, set to -1
                    else if (value <= -0.5f && value > -1.0f) {
                        return -1.0f;
                    }
                    // For other values, return as is
                    return value;
                    };

                // Processing for the X-axis
                float mouseX = mouseRel.x();
                // We don't use abs() here to preserve the sign of the movement
                // This allows us to detect and process even very small movements in either direction
                if (mouseX != 0) {
                    // Scale the mouse X movement
                    float scaledMouseX = mouseX * SENSITIVITY_FACTOR;
                    // Adjust the scaled value to ensure minimal movement is registered
                    scaledMouseX = adjustMouseInput(scaledMouseX);
                    // Convert to 16-bit integer and write the adjusted X value to the NDS memory
                    NDS->ARM9Write16(aimXAddr, static_cast<uint16_t>(scaledMouseX));
                    enableAim = true;
                }

                // Processing for the Y-axis
                float mouseY = mouseRel.y();
                // Again, we avoid using abs() to maintain directional information
                // This ensures that even slight movements are captured and processed
                if (mouseY != 0) {
                    // Scale the mouse Y movement and apply aspect ratio correction
                    float scaledMouseY = mouseY * aimAspectRatio * SENSITIVITY_FACTOR;
                    // Adjust the scaled value to ensure minimal movement is registered
                    scaledMouseY = adjustMouseInput(scaledMouseY);
                    // Convert to 16-bit integer and write the adjusted Y value to the NDS memory
                    NDS->ARM9Write16(aimYAddr, static_cast<uint16_t>(scaledMouseY));
                    enableAim = true;
                }

                // Move hunter
                processMoveInput();

                // Shoot
                if (Input::HotkeyDown(HK_MetroidShootScan) || Input::HotkeyDown(HK_MetroidScanShoot)) {
                    FN_INPUT_PRESS(INPUT_L);
                }
                else {
                    FN_INPUT_RELEASE(INPUT_L);
                }

                // Zoom, map zoom out
                if (Input::HotkeyDown(HK_MetroidZoom)) {
                    FN_INPUT_PRESS(INPUT_R);
                }
                else {
                    FN_INPUT_RELEASE(INPUT_R);
                }

                // Jump
                if (Input::HotkeyDown(HK_MetroidJump)) {
                    FN_INPUT_PRESS(INPUT_B);
                }
                else {
                    FN_INPUT_RELEASE(INPUT_B);
                }

                // Alt-form
                if (Input::HotkeyPressed(HK_MetroidMorphBall)) {

                    NDS->ReleaseScreen();
                    frameAdvance(2);
                    NDS->TouchScreen(231, 167);
                    frameAdvance(2);

                    if (isSamus) {
                        enableAim = false; // in case isAltForm isnt immediately true

                        // boost ball doesnt work unless i release screen late enough
                        for (int i = 0; i < 4; i++) {
                            frameAdvance(2);
                            NDS->ReleaseScreen();
                        }

                    }
                }



                // Define a lambda function to switch weapons
                auto SwitchWeapon = [&](int weaponIndex) {

                    // Check for Already equipped
                    if (NDS->ARM9Read8(selectedWeaponAddr) == weaponIndex) {
                        // mainWindow->osdAddMessage(0, "Weapon switch unnecessary: Already equipped");
                        return; // Early return if the weapon is already equipped
                    }

                    // Check isMapOrUserActionPaused, for the issue "If you switch weapons while the map is open, the aiming mechanism may become stuck."
                    if (isInAdventure && NDS->ARM9Read8(isMapOrUserActionPausedAddr) == 0x1) {
                        return;
                    }

                    // Read the current jump flag value
                    uint8_t currentFlags = NDS->ARM9Read8(jumpFlagAddr);

                    // Check if the upper 4 bits are odd (1 or 3)
                    // this is for fixing issue: Shooting and transforming become impossible, when changing weapons at high speed while transitioning from transformed to normal form.
                    bool isTransforming = currentFlags & 0x10;

                    uint8_t jumpFlag = currentFlags & 0x0F;  // Get the lower 4 bits
                    //mainWindow->osdAddMessage(0, ("JumpFlag:" + std::string(1, "0123456789ABCDEF"[NDS->ARM9Read8(jumpFlagAddr) & 0x0F])).c_str());

                    bool needToRestore = false;

                    // Check if in alternate form (transformed state)
                    isAltForm = NDS->ARM9Read8(isAltFormAddr) == 0x02;

                    // If not jumping (jumpFlag == 0) and in normal form, temporarily set to jumped state (jumpFlag == 1)
                    if (!isTransforming && jumpFlag == 0 && !isAltForm) {
                        uint8_t newFlags = (currentFlags & 0xF0) | 0x01;  // Set lower 4 bits to 1
                        NDS->ARM9Write8(jumpFlagAddr, newFlags);
                        needToRestore = true;
                        //mainWindow->osdAddMessage(0, ("JumpFlag:" + std::string(1, "0123456789ABCDEF"[NDS->ARM9Read8(jumpFlagAddr) & 0x0F])).c_str());
                        //mainWindow->osdAddMessage(0, "Done setting jumpFlag.");
                    }

                    // Release the screen (for weapon change)
                    NDS->ReleaseScreen();

                    // Lambda to set the weapon-changing state
                    auto setChangingWeapon = [](int value) -> int {
                        // Apply mask to set the lower 4 bits to 1011 (B in hexadecimal)
                        return (value & 0xF0) | 0x0B; // Keep the upper 4 bits, set lower 4 bits to 1011
                        };

                    // Modify the value using the lambda
                    int valueOfWeaponChange = setChangingWeapon(NDS->ARM9Read8(weaponChangeAddr));

                    // Write the weapon change command to ARM9
                    NDS->ARM9Write8(weaponChangeAddr, valueOfWeaponChange); // Only change the lower 4 bits to B

                    // Change the weapon
                    NDS->ARM9Write8(selectedWeaponAddr, weaponIndex);  // Write the address of the corresponding weapon

                    // Advance frames (for reflection of ReleaseScreen, WeaponChange)
                    frameAdvance(2);

                    // Need Touch after ReleaseScreen for aiming.
                    NDS->TouchScreen(128, 96);

                    // Advance frames (for reflection of Touch. This is necessary for no jump)
                    frameAdvance(2);

                    // Restore the jump flag to its original value (if necessary)
                    if (needToRestore) {
                        currentFlags = NDS->ARM9Read8(jumpFlagAddr);
                        uint8_t restoredFlags = (currentFlags & 0xF0) | jumpFlag;
                        NDS->ARM9Write8(jumpFlagAddr, restoredFlags);
                        //mainWindow->osdAddMessage(0, ("JumpFlag:" + std::string(1, "0123456789ABCDEF"[NDS->ARM9Read8(jumpFlagAddr) & 0x0F])).c_str());
                        //mainWindow->osdAddMessage(0, "Restored jumpFlag.");

                    }

                    };

                // Switch to Power Beam
                if (Input::HotkeyPressed(HK_MetroidWeaponBeam)) {
                    SwitchWeapon(0);
                }

                // Switch to Missile
                if (Input::HotkeyPressed(HK_MetroidWeaponMissile)) {
                    SwitchWeapon(2);

                }

                // Array of sub-weapon hotkeys (Associating hotkey definitions with weapon indices)
                Hotkey weaponHotkeys[] = {
                    HK_MetroidWeapon1,  // ShockCoil    7
                    HK_MetroidWeapon2,  // Magmaul      6
                    HK_MetroidWeapon3,  // Judicator    5
                    HK_MetroidWeapon4,  // Imperialist  4
                    HK_MetroidWeapon5,  // Battlehammer 3
                    HK_MetroidWeapon6   // VoltDriver   1
                                        // Omega Cannon 8 we don't need to set this here, because we need {last used weapon / Omega cannon}
                };

                int weaponIndices[] = { 7, 6, 5, 4, 3, 1 };  // Address of the weapon corresponding to each hotkey

                // Sub-weapons processing (handled in a loop)
                for (int i = 0; i < 6; i++) {
                    if (Input::HotkeyPressed(weaponHotkeys[i])) {
                        SwitchWeapon(weaponIndices[i]);  // Switch to the corresponding weapon

                        // Exit loop when hotkey is pressed (because weapon switching is completed)
                        break;
                    }
                }

                // Change to loaded SpecialWeapon, Last used weapon or Omega Canon
                if (Input::HotkeyPressed(HK_MetroidWeaponSpecial)) {
                    uint8_t loadedSpecialWeapon = NDS->ARM9Read8(loadedSpecialWeaponAddr);
                    if(loadedSpecialWeapon != 0xFF){
                        // switchWeapon if special weapon is loaded
                        SwitchWeapon(loadedSpecialWeapon);
                    }
                }

                // Morph ball boost
                if (isSamus && Input::HotkeyDown(HK_MetroidHoldMorphBallBoost))
                {
                    isAltForm = NDS->ARM9Read8(isAltFormAddr) == 0x02;
                    if (isAltForm) {
                        uint8_t boostGaugeValue = NDS->ARM9Read8(boostGaugeAddr);
                        bool isBoosting = NDS->ARM9Read8(isBoostingAddr) != 0x00;

                        // boostable when gauge value is 0x05-0x0F(max)
                        bool isBoostGaugeEnough = boostGaugeValue > 0x0A;

                        // just incase
                        enableAim = false;

                        // release for boost?
                        NDS->ReleaseScreen();

                        if (!isBoosting && isBoostGaugeEnough) {
                            // do boost by releasing boost key
                            FN_INPUT_RELEASE(INPUT_R);
                        }
                        else {
                            // charge boost gauge by holding boost key
                            FN_INPUT_PRESS(INPUT_R);
                        }

                        if (isBoosting) {
                            // touch again for aiming
                            NDS->TouchScreen(128, 96); // required for aiming
                        }

                    }
                }

                // Start / View Match progress, points
                if (Input::HotkeyDown(HK_MetroidMenu)) {
                    FN_INPUT_PRESS(INPUT_START);
                }
                else {
                    FN_INPUT_RELEASE(INPUT_START);
                }



                if (isInAdventure) {
                    // Adventure Mode Functions


                    // Scan Visor
                    if (Input::HotkeyPressed(HK_MetroidScanVisor)) {
                        NDS->ReleaseScreen();
                        frameAdvance(2);

                        bool inVisor = NDS->ARM9Read8(inVisorOrMapAddr) == 0x1;
                        // mainWindow->osdAddMessage(0, "in visor %d", inVisor);

                        NDS->TouchScreen(128, 173);

                        if (inVisor) {
                            frameAdvance(2);
                        }
                        else {
                            for (int i = 0; i < 30; i++) {
                                // still allow movement whilst we're enabling scan visor
                                processMoveInput();
                                NDS->SetKeyMask(Input::GetInputMask());

                                frameAdvanceOnce();
                            }
                        }

                        NDS->ReleaseScreen();
                        frameAdvance(2);
                    }

                    // OK (in scans and messages)
                    if (Input::HotkeyPressed(HK_MetroidUIOk)) {
                        NDS->ReleaseScreen();
                        frameAdvance(2);
                        NDS->TouchScreen(128, 142);
                        frameAdvance(2);
                    }

                    // Left arrow (in scans and messages)
                    if (Input::HotkeyPressed(HK_MetroidUILeft)) {
                        NDS->ReleaseScreen();
                        frameAdvance(2);
                        NDS->TouchScreen(71, 141);
                        frameAdvance(2);
                    }

                    // Right arrow (in scans and messages)
                    if (Input::HotkeyPressed(HK_MetroidUIRight)) {
                        NDS->ReleaseScreen();
                        frameAdvance(2);
                        NDS->TouchScreen(185, 141); // optimization ?
                        frameAdvance(2);
                    }

                    // Enter to Starship
                    if (Input::HotkeyPressed(HK_MetroidUIYes)) {
                        NDS->ReleaseScreen();
                        frameAdvance(2);
                        NDS->TouchScreen(96, 142);
                        frameAdvance(2);
                    }

                    // No Enter to Starship
                    if (Input::HotkeyPressed(HK_MetroidUINo)) {
                        NDS->ReleaseScreen();
                        frameAdvance(2);
                        NDS->TouchScreen(160, 142);
                        frameAdvance(2);
                    }
                } // End of Adventure Functions

                // End of in-game
			}
			else {
                // VirtualStylus


                // this exists to just delay the pressing of the screen when you
                // release the virtual stylus key
                enableAim = false;

                drawVCur = true;

                if (Input::HotkeyDown(HK_MetroidShootScan) || Input::HotkeyDown(HK_MetroidScanShoot)) {
                    NDS->TouchScreen(virtualStylusX, virtualStylusY);
                }
                else {
                    NDS->ReleaseScreen();
                }

                // mouse (VirtualStylus)

                mouseX = mouseRel.x();

                if (abs(mouseX) > 0) {
                    virtualStylusX += (
                        mouseX * SENSITIVITY_FACTOR_VIRTUAL_STYLUS
                        );
                }

                mouseY = mouseRel.y();

                if (abs(mouseY) > 0) {
                    virtualStylusY += (
                        mouseY * dsAspectRatio * SENSITIVITY_FACTOR_VIRTUAL_STYLUS
                        );
                }

                if (virtualStylusX < 0) virtualStylusX = 0;
                if (virtualStylusX > 255) virtualStylusX = 255;
                if (virtualStylusY < 0) virtualStylusY = 0;
                if (virtualStylusY > 191) virtualStylusY = 191;


			} // End of isVirtualStylusEnabled


		}// END of if(isFocused)

        // Touch again for aiming
        isAltForm = NDS->ARM9Read8(isAltFormAddr) == 0x02;
        if (!wasLastFrameFocused || enableAim) {
            // touch again for aiming
            // When you return to melonPrimeDS or normal form

            // mainWindow->osdAddMessage(0,"touching screen for aim");

            // Changed Y point center(96) to 88, For fixing issue: Alt Tab switches hunter choice.
            //NDS->TouchScreen(128, 96); // required for aiming
            NDS->TouchScreen(128, 88); // required for aiming
        }

        NDS->SetKeyMask(Input::GetInputMask());

        // Showing Virtual Stylus
        if (screenGL) {
            // OpenGL
            screenGL->virtualCursorShow = drawVCur;
            screenGL->virtualCursorX = virtualStylusX;
            screenGL->virtualCursorY = virtualStylusY;

        } else if (drawVCur) {
            // no OpenGL
            
            // TODO Fix that drawVCur is not working with limited Framerate
            // TODO If OpenGL is not used, Virtual Stylus is only visible when the frame rate limit is removed.

            const int cursorSize = virtualCursorSize;
            const int cursorOffset = virtualCursorSize / 2;

            auto setPixel {
                [&](int x, int y, melonDS::u32 color) {
                    if (x < 0) return;
                    if (x > 255) return;
                    if (y < 0) return;
                    if (y > 191) return;
                    if (NDS->GPU.GPU3D.IsRendererAccelerated()) {
                        NDS->GPU.Framebuffer[0][1][y * (256 * 3 + 1) + x] = color;
                        NDS->GPU.Framebuffer[1][1][y * (256 * 3 + 1) + x] = color;
                    } else {
                        NDS->GPU.Framebuffer[0][1][y * 256 + x] = color;
                        NDS->GPU.Framebuffer[1][1][y * 256 + x] = color;
                    }
                }
            };

            for (int y = 0; y < cursorSize; y++) {
                for (int x = 0; x < cursorSize; x++) {
                    int value = virtualCursorPixels[y * cursorSize + x];
                    if (!value) continue;
                    setPixel(
                        virtualStylusX + x - cursorOffset,
                        virtualStylusY + y - cursorOffset,
                        0xFFFFFFFF
                    );
                }
            }
        }

        // record last frame was forcused or not
        wasLastFrameFocused = isFocused;

        frameAdvanceOnce();

    } // End of while (EmuRunning != emuStatus_Exit)

    file = Platform::OpenLocalFile("rtc.bin", Platform::FileMode::Write);
    if (file)
    {
        RTC::StateData state;
        NDS->RTC.GetState(state);
        Platform::FileWrite(&state, sizeof(state), 1, file);
        Platform::CloseFile(file);
    }

    // rawInputThread->quit();

    EmuStatus = emuStatus_Exit;

    NDS::Current = nullptr;
    // nds is out of scope, so unique_ptr cleans it up for us
}

void EmuThread::changeWindowTitle(char* title)
{
    emit windowTitleChange(QString(title));
}

void EmuThread::emuRun()
{
    EmuRunning = emuStatus_Running;
    EmuPauseStack = EmuPauseStackRunning;
    RunningSomething = true;

    // checkme
    emit windowEmuStart();
    AudioInOut::Enable();
}

void EmuThread::initContext()
{
    ContextRequest = contextRequest_InitGL;
    while (ContextRequest != contextRequest_None);
}

void EmuThread::deinitContext()
{
    ContextRequest = contextRequest_DeInitGL;
    while (ContextRequest != contextRequest_None);
}

void EmuThread::emuPause()
{
    EmuPauseStack++;
    if (EmuPauseStack > EmuPauseStackPauseThreshold) return;

    PrevEmuStatus = EmuRunning;
    EmuRunning = emuStatus_Paused;
    while (EmuStatus != emuStatus_Paused);

    AudioInOut::Disable();
}

void EmuThread::emuUnpause()
{
    if (EmuPauseStack < EmuPauseStackPauseThreshold) return;

    EmuPauseStack--;
    if (EmuPauseStack >= EmuPauseStackPauseThreshold) return;

    EmuRunning = PrevEmuStatus;

    AudioInOut::Enable();
}

void EmuThread::emuStop()
{
    EmuRunning = emuStatus_Exit;
    EmuPauseStack = EmuPauseStackRunning;

    AudioInOut::Disable();
}

void EmuThread::emuFrameStep()
{
    if (EmuPauseStack < EmuPauseStackPauseThreshold) emit windowEmuPause();
    EmuRunning = emuStatus_FrameStep;
}

bool EmuThread::emuIsRunning()
{
    return EmuRunning == emuStatus_Running;
}

bool EmuThread::emuIsActive()
{
    return (RunningSomething == 1);
}
