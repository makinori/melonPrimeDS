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
    HK_SolarSensorIncrease,
    HK_SolarSensorDecrease,

    HK_MetroidMorphBall,
    HK_MetroidScanVisor,
    HK_MetroidUIOk,
    HK_MetroidUILeft,
    HK_MetroidUIRight,
    HK_MetroidWeaponCycle,
    HK_MetroidSubweaponPrevious,
    HK_MetroidSubweaponNext,
    HK_MetroidLeftStickXAxis,
    HK_MetroidLeftStickYAxis,
    HK_MetroidRightStickXAxis,
    HK_MetroidRightStickYAxis,
    HK_MetroidShoot,
    HK_MetroidMorphBallBoost,
    HK_MetroidVirtualStylus,
    HK_MetroidJump,
    HK_MetroidMenu,
};

static constexpr std::initializer_list<const char*> hk_addons_labels =
{
    "[Boktai] Sunlight + ",
    "[Boktai] Sunlight - ",
    
    "[Metroid] Morph Ball",
    "[Metroid] Scan Visor",
    "[Metroid] UI Ok",
    "[Metroid] UI Left",
    "[Metroid] UI Right",
    "[Metroid] Weapon Cycle",
    "[Metroid] Subweapon Previous",
    "[Metroid] Subweapon Next",
    "[Metroid] Left Stick X Axis",
    "[Metroid] Left Stick Y Axis",
    "[Metroid] Right Stick X Axis",
    "[Metroid] Right Stick Y Axis",
    "[Metroid] Shoot",
    "[Metroid] Morph Ball Boost",
    "[Metroid] Virtual Stylus (Hold)",
    "[Metroid] Jump",
    "[Metroid] Menu",
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

private slots:
    void on_InputConfigDialog_accepted();
    void on_InputConfigDialog_rejected();

    void on_btnKeyMapSwitch_clicked();
    void on_btnJoyMapSwitch_clicked();
    void on_cbxJoystick_currentIndexChanged(int id);

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
