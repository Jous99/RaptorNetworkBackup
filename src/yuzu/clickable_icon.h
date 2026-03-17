// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QIcon>
#include <QLabel>
#include <QString>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QString;
class QWidget;

class ClickableIcon : public QLabel {
    Q_OBJECT

public:
    using QLabel::QLabel;

signals:
    void activated();

protected:
    bool event(QEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;

    void paintEvent(QPaintEvent* event) override;

private:
    QIcon icon;
    QIcon icon_hover;

    bool icons_loaded = false;
    bool hover = false;
};