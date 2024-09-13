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

#ifndef MAIN_H
#define MAIN_H

#include "glad/glad.h"

#include <QApplication>
#include <QWidget>
#include <QWindow>
#include <QMainWindow>
#include <QImage>
#include <QActionGroup>
#include <QTimer>
#include <QScreen>
#include <QCloseEvent>
#include <cstdint> // added for romversions

#include "Window.h"
#include "EmuThread.h"
#include "FrontendUtil.h"

class MelonApplication : public QApplication
{
    Q_OBJECT

public:
    MelonApplication(int &argc, char** argv);
    bool event(QEvent* event) override;
};

// グローバル変数をexternで宣言（他のファイルから参照可能にする）
extern unsigned int globalChecksum;

// グローバル変数をexternで宣言（他のファイルから参照可能にする）
extern bool isNewRom;
extern bool isVirtualStylusEnabled;

// 他のファイルで使用するためにexternを使用して宣言する



namespace RomVersions {
    constexpr uint32_t USA1_0 = 0x218DA42C;
    constexpr uint32_t USA1_1 = 0x91B46577;
    constexpr uint32_t EU1_0 = 0xA4A8FE5A;
    constexpr uint32_t EU1_1 = 0x910018A5;
    constexpr uint32_t JAPAN1_0 = 0xD75F539D;
    constexpr uint32_t JAPAN1_1 = 0x42EBF348;
    constexpr uint32_t KOREA1_0 = 0xE54682F3;
}



#endif // MAIN_H
