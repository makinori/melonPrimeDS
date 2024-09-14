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

#ifndef INPUTCONFIGDIALOG_H
#define INPUTCONFIGDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <initializer_list>

#include "Config.h"

static constexpr int keypad_num = 12;

static constexpr std::initializer_list<int> hk_addons =
{
    // HK_SolarSensorIncrease,
    // HK_SolarSensorDecrease,

    HK_MetroidMoveForward,
    HK_MetroidMoveBack,
    HK_MetroidMoveLeft,
    HK_MetroidMoveRight,

    HK_MetroidJump,

    HK_MetroidMorphBall,
    HK_MetroidMorphBallBoost,

    HK_MetroidScanVisor,
    
    HK_MetroidUILeft,
    HK_MetroidUIRight,
    HK_MetroidUIOk,

    HK_MetroidShootScan,
    HK_MetroidScanShoot,

    HK_MetroidWeaponBeam,
    HK_MetroidWeaponMissile,
    HK_MetroidWeapon1,
    HK_MetroidWeapon2,
    HK_MetroidWeapon3,
    HK_MetroidWeapon4,
    HK_MetroidWeapon5,
    HK_MetroidWeapon6,
    HK_MetroidWeapon7,
    
    HK_MetroidVirtualStylus,
    HK_MetroidMenu,
};

static constexpr std::initializer_list<const char*> hk_addons_labels =
{
    // "[Boktai] Sunlight + ",
    // "[Boktai] Sunlight - ",

    "[Metroid] (W) Move Forward",
    "[Metroid] (S) Move Back",
    "[Metroid] (A) Move Left",
    "[Metroid] (D) Move Right",

    "[Metroid] (Space) Jump",

    "[Metroid] (L. Ctrl) Morph Ball",
    "[Metroid] (L. Shift) Morph Ball Boost, Map Zoom Out, Imperialist Zoom",

    "[Metroid] (C) Scan Visor",

    "[Metroid] (Q) UI Left",
    "[Metroid] (E) UI Right",
    "[Metroid] (F) UI Ok",

    "[Metroid] (Mouse Left) Shoot/Scan, Map Zoom In)",
    "[Metroid] (Mouse Right) Scan/Shoot, Map Zoom In)",

    "[Metroid] (Mouse 5, Side Top) Weapon Beam",
    "[Metroid] (Mouse 4, Side Bottom) Weapon Missile",
    "[Metroid] (1) Weapon 1",
    "[Metroid] (2) Weapon 2",
    "[Metroid] (3) Weapon 3",
    "[Metroid] (4) Weapon 4",
    "[Metroid] (5) Weapon 5",
    "[Metroid] (6) Weapon 6",
    "[Metroid] (7) Last Weapon used/Omega Canon",

    "[Metroid] (Tab) Virtual Stylus [Toggle]",
    "[Metroid] (V) Menu/Map",
};

static_assert(hk_addons.size() == hk_addons_labels.size());

static constexpr std::initializer_list<int> hk_general =
{
    HK_Pause,
    HK_Reset,
    HK_FrameStep,
    HK_FastForward,
    HK_FastForwardToggle,
    HK_FullscreenToggle,
    HK_Lid,
    HK_Mic,
    HK_SwapScreens,
    HK_SwapScreenEmphasis,
    HK_PowerButton,
    HK_VolumeUp,
    HK_VolumeDown
};

static constexpr std::initializer_list<const char*> hk_general_labels =
{
    "Pause/resume",
    "Reset",
    "Frame step",
    "Fast forward",
    "Toggle FPS limit",
    "Toggle fullscreen",
    "Close/open lid",
    "Microphone",
    "Swap screens",
    "Swap screen emphasis",
    "DSi Power button",
    "DSi Volume up",
    "DSi Volume down"
};

static_assert(hk_general.size() == hk_general_labels.size());


namespace Ui { class InputConfigDialog; }
class InputConfigDialog;

class InputConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InputConfigDialog(QWidget* parent);
    ~InputConfigDialog();

    static InputConfigDialog* currentDlg;
    static InputConfigDialog* openDlg(QWidget* parent)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            return currentDlg;
        }

        currentDlg = new InputConfigDialog(parent);
        currentDlg->open();
        return currentDlg;
    }
    static void closeDlg()
    {
        currentDlg = nullptr;
    }

    void switchTabToAddons();
    void switchTabToMetroid();

private slots:
    void on_InputConfigDialog_accepted();
    void on_InputConfigDialog_rejected();

    void on_btnKeyMapSwitch_clicked();
    void on_btnJoyMapSwitch_clicked();
    void on_cbxJoystick_currentIndexChanged(int id);

    void on_metroidResetSensitivityValues_clicked();

private:
    void populatePage(QWidget* page,
        const std::initializer_list<const char*>& labels,
        int* keymap, int* joymap);
    void setupKeypadPage();

    Ui::InputConfigDialog* ui;

    int keypadKeyMap[12], keypadJoyMap[12];
    int addonsKeyMap[hk_addons.size()], addonsJoyMap[hk_addons.size()];
    int hkGeneralKeyMap[hk_general.size()], hkGeneralJoyMap[hk_general.size()];
};


#endif // INPUTCONFIGDIALOG_H
