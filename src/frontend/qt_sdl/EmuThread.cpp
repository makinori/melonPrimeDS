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

// TODO: uniform variable spelling
using namespace melonDS;

// TEMP
extern bool RunningSomething;
extern MainWindow* mainWindow;
extern int autoScreenSizing;
extern int videoRenderer;
extern bool videoSettingsDirty;


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

        if (EmuRunning == emuStatus_Running || EmuRunning == emuStatus_FrameStep)
        {
            EmuStatus = emuStatus_Running;
            if (EmuRunning == emuStatus_FrameStep) EmuRunning = emuStatus_Paused;

            if (Input::HotkeyPressed(HK_SolarSensorDecrease))
            {
                int level = NDS->GBACartSlot.SetInput(GBACart::Input_SolarSensorDown, true);
                if (level != -1)
                {
                    mainWindow->osdAddMessage(0, "Solar sensor level: %d", level);
                }
            }
            if (Input::HotkeyPressed(HK_SolarSensorIncrease))
            {
                int level = NDS->GBACartSlot.SetInput(GBACart::Input_SolarSensorUp, true);
                if (level != -1)
                {
                    mainWindow->osdAddMessage(0, "Solar sensor level: %d", level);
                }
            }

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
                    sprintf(melontitle, "[%d/%.0f] melonDS (Metroid Prime Hunters) " MELONDS_VERSION, fps, fpstarget);
                else
                    sprintf(melontitle, "[%d/%.0f] melonDS (Metroid Prime Hunters) (%d)", fps, fpstarget, inst+1);
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
                sprintf(melontitle, "melonDS (Metroid Prime Hunters) " MELONDS_VERSION);
            else
                sprintf(melontitle, "melonDS (Metroid Prime Hunters) (%d)", inst+1);
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

#define METROID_US_1_1 1
#ifdef METROID_US_1_1
    const melonDS::u32 aimXAddr = 0x020DEDA6;
    const melonDS::u32 aimYAddr = 0x020DEDAE;
    const melonDS::u32 ballAddr = 0x020DB098;
    const melonDS::u32 visorAddr = 0x020D9A7D; // my best guess
#else
    const melonDS::u32 aimXAddr = 0x020DE526;
    const melonDS::u32 aimYAddr = 0x020DE52E;
    const melonDS::u32 ballAddr = 0x020DA818;
#endif

// #define ENABLE_MEMORY_DUMP 1
#ifdef ENABLE_MEMORY_DUMP
    int memoryDump = 0;
#endif

    bool enableAim = true;

    float virtualStylusX = 128;
    float virtualStylusY = 96;

    bool focusedLastFrame = false;

    const float dsAspectRatio = 256.0 / 192.0; 
    const float aimAspectRatio = 6.0 / 4.0; // i have no idea

    // RawInputThread* rawInputThread = new RawInputThread(parent());
    // rawInputThread->start();

    auto processMoveInput
    {
    []() {
        if (Input::HotkeyDown(HK_MetroidMoveForward)) {
            FN_INPUT_PRESS(INPUT_UP);
        } else {
            FN_INPUT_RELEASE(INPUT_UP);
        }

        if (Input::HotkeyDown(HK_MetroidMoveBack)) {
            FN_INPUT_PRESS(INPUT_DOWN);
        } else {
            FN_INPUT_RELEASE(INPUT_DOWN);
        }

        if (Input::HotkeyDown(HK_MetroidMoveLeft)) {
            FN_INPUT_PRESS(INPUT_LEFT);
        } else {
            FN_INPUT_RELEASE(INPUT_LEFT);
        }

        if (Input::HotkeyDown(HK_MetroidMoveRight)) {
            FN_INPUT_PRESS(INPUT_RIGHT);
        } else {
            FN_INPUT_RELEASE(INPUT_RIGHT);
        }
    }
    };

    while (EmuRunning != emuStatus_Exit) {
        // auto mouseRel = rawInputThread->fetchMouseDelta();
        QPoint mouseRel;

        auto isFocused = mainWindow->panel->getFocused();

        if (isFocused) {
            auto windowCenterX = mainWindow->pos().x() + mainWindow->size().width() / 2;
            auto windowCenterY = mainWindow->pos().y() + mainWindow->size().height() / 2;
            // if (!focusedLastFrame) {
            //     // fetch will flush but discard values
            //     mouseRel.first = 0;
            //     mouseRel.second = 0;
            // }
            if (focusedLastFrame) {
                mouseRel = QCursor::pos() - QPoint(windowCenterX, windowCenterY);
            }
            QCursor::setPos(windowCenterX, windowCenterY);
        }

        focusedLastFrame = isFocused;

        bool drawVCur = false;

#ifdef ENABLE_MEMORY_DUMP
        if (Input::HotkeyPressed(HK_MetroidVirtualStylus)) {
            printf("MainRAMMask 0x%.8" PRIXPTR "\n", (uintptr_t)NDS->MainRAMMask);
            QFile file("memory" + QString::number(memoryDump++) + ".bin");
            if (file.open(QIODevice::ReadWrite)) {
                file.write(QByteArray((char*)NDS->MainRAM, NDS->MainRAMMaxSize));
            }
        }
     
        if (false) {
#else
        if (isFocused && Input::HotkeyDown(HK_MetroidVirtualStylus)) {
#endif

            // this exists to just delay the pressing of the screen when you 
            // release the virtual stylus key
            enableAim = false;

            drawVCur = true;

            if (Input::HotkeyDown(HK_MetroidShootScan) || Input::HotkeyDown(HK_MetroidScanShoot)) {
                NDS->TouchScreen(virtualStylusX, virtualStylusY);
            } else {
                NDS->ReleaseScreen();
            }

            // mouse

            if (abs(mouseRel.x()) > 0) {
                virtualStylusX += (
                    mouseRel.x() * Config::MetroidVirtualStylusSensitivity * 0.01
                );
            }

            if (abs(mouseRel.y()) > 0) {
                virtualStylusY += (
                    mouseRel.y() * dsAspectRatio * Config::MetroidVirtualStylusSensitivity * 0.01
                );
            }

            if (virtualStylusX < 0) virtualStylusX = 0;
            if (virtualStylusX > 255) virtualStylusX = 255;
            if (virtualStylusY < 0) virtualStylusY = 0;
            if (virtualStylusY > 191) virtualStylusY = 191;
        } else if (isFocused) {
            drawVCur = false;

            // morph ball
            if (Input::HotkeyPressed(HK_MetroidMorphBall)) {
                enableAim = false; // in case inBall isnt immediately true
                NDS->ReleaseScreen();
                frameAdvance(2);
                NDS->TouchScreen(231, 167);
                // boost ball doesnt work unless i release screen late enough
                for (int i = 0; i < 4; i++) {
                    frameAdvance(2);
                    NDS->ReleaseScreen();
                }
            }

            // scan visor
            if (Input::HotkeyPressed(HK_MetroidScanVisor)) {
                NDS->ReleaseScreen();
                frameAdvance(2);

                bool inVisor = NDS->ARM9Read8(visorAddr) == 0x1;
                // mainWindow->osdAddMessage(0, "in visor %d", inVisor);

                NDS->TouchScreen(128, 173);

                if (inVisor) {
                    frameAdvance(2);
                } else {
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

            // ok (in scans and messages)
            if (Input::HotkeyPressed(HK_MetroidUIOk)) {
                NDS->ReleaseScreen();
                frameAdvance(2);
                NDS->TouchScreen(128, 142);
                frameAdvance(2);
            }

            // left arrow (in scans and messages)
            if (Input::HotkeyPressed(HK_MetroidUILeft)) {
                NDS->ReleaseScreen();
                frameAdvance(2);
                NDS->TouchScreen(71, 141);
                frameAdvance(2);
            }

            // right arrow (in scans and messages)
            if (Input::HotkeyPressed(HK_MetroidUIRight)) {
                NDS->ReleaseScreen();
                frameAdvance(2);
                NDS->TouchScreen(185, 141);
                frameAdvance(2);
            }

            // switch to beam
            if (Input::HotkeyPressed(HK_MetroidWeaponBeam)) {
                NDS->ReleaseScreen();
                frameAdvance(2);
                NDS->TouchScreen(85 + 40 * 0, 32);
                frameAdvance(2);
                NDS->ReleaseScreen();
                frameAdvance(2);
            }

            // switch to missiles
            if (Input::HotkeyPressed(HK_MetroidWeaponMissile)) {
                NDS->ReleaseScreen();
                frameAdvance(2);
                NDS->TouchScreen(85 + 40 * 1, 32);
                frameAdvance(2);
                NDS->ReleaseScreen();
                frameAdvance(2);
            }

            // switch subweapon

            Hotkey weaponHotkeys[] = {
                HK_MetroidWeapon1,
                HK_MetroidWeapon2,
                HK_MetroidWeapon3,
                HK_MetroidWeapon4,
                HK_MetroidWeapon5,
                HK_MetroidWeapon6,
            };

            for (int i = 0; i < 6; i++) {
                if (Input::HotkeyPressed(weaponHotkeys[i])) {
                    melonDS::u16 subX = 93 + 25 * i;
                    melonDS::u16 subY = 48 + 25 * i;

                    NDS->ReleaseScreen();
                    frameAdvance(2);
                    NDS->TouchScreen(232, 34);
                    frameAdvance(2);
                    NDS->TouchScreen(subX, subY);
                    frameAdvance(2);
                    NDS->ReleaseScreen();
                    frameAdvance(2);
                }
            }

            // move

            processMoveInput();

            // cursor looking
            
            if (abs(mouseRel.x()) > 0) {
                NDS->ARM9Write32(
                    aimXAddr, mouseRel.x() * Config::MetroidAimSensitivity * 0.01
                );
                enableAim = true;
            }

            if (abs(mouseRel.y()) > 0) {
                NDS->ARM9Write32(
                    aimYAddr, mouseRel.y() * aimAspectRatio * Config::MetroidAimSensitivity * 0.01
                );
                enableAim = true;
            }

            // morph ball boost
            if (Input::HotkeyDown(HK_MetroidMorphBallBoost)) {
                // just incase
                enableAim = false;
                NDS->ReleaseScreen();
                // then press input
                FN_INPUT_PRESS(INPUT_R);
            } else {
                FN_INPUT_RELEASE(INPUT_R);
            }

            // shoot
            if (Input::HotkeyDown(HK_MetroidShootScan) || Input::HotkeyDown(HK_MetroidScanShoot)) {
                FN_INPUT_PRESS(INPUT_L);
            } else {
                FN_INPUT_RELEASE(INPUT_L);
            }

            // jump
            if (Input::HotkeyDown(HK_MetroidJump)) {
                FN_INPUT_PRESS(INPUT_B);
            } else {
                FN_INPUT_RELEASE(INPUT_B);
            }

            // start
            if (Input::HotkeyDown(HK_MetroidMenu)) {
                FN_INPUT_PRESS(INPUT_START);
            } else {
                FN_INPUT_RELEASE(INPUT_START);
            }
        
        }

        // is this a good way of detecting morph ball status?
        bool inBall = NDS->ARM9Read8(ballAddr) == 0x02;
        if (!inBall && enableAim) {
            // mainWindow->osdAddMessage(0,"touching screen for aim");
            NDS->TouchScreen(128, 96); // required for aiming
        }
        
        NDS->SetKeyMask(Input::GetInputMask());

        if (drawVCur) { 
            const int cursorSize = 11;
            const int cursorOffset = 5;
            const bool cursor[] = {
                0,0,0,1,1,1,1,1,0,0,0,
                0,0,1,0,0,0,0,0,1,0,0,
                0,1,0,0,0,0,0,0,0,1,0,
                1,0,0,0,0,0,0,0,0,0,1,
                1,0,0,0,0,1,0,0,0,0,1,
                1,0,0,0,1,1,1,0,0,0,1,
                1,0,0,0,0,1,0,0,0,0,1,
                1,0,0,0,0,0,0,0,0,0,1,
                0,1,0,0,0,0,0,0,0,1,0,
                0,0,1,0,0,0,0,0,1,0,0,
                0,0,0,1,1,1,1,1,0,0,0,
            };

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
                    int value = cursor[y * cursorSize + x];
                    if (!value) continue;
                    setPixel(
                        virtualStylusX + x - cursorOffset,
                        virtualStylusY + y - cursorOffset,
                        0xFFFFFFFF
                    );
                }
            }
        }

        frameAdvanceOnce();
    }

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
