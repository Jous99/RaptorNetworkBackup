// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QWidget>

class QMenuBar;

class Overlay : public QWidget {
    Q_OBJECT

public:
    Overlay(QWidget* parent = nullptr);
    virtual ~Overlay() = default;

    virtual void Reposition(QWidget*, QMenuBar*) {}
};
