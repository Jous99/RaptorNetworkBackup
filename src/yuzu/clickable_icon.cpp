// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QString>
#include <QWidget>

#include "yuzu/clickable_icon.h"

bool ClickableIcon::event(QEvent* event) {
    switch (const auto type = event->type(); type) {
    case QEvent::Enter:
    case QEvent::Leave:
        hover = type == QEvent::Enter;
        repaint();
        break;
    default:
        break;
    }
    return QLabel::event(event);
}

void ClickableIcon::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MouseButton::LeftButton) {
        clearFocus();
        emit activated();
    }
}

void ClickableIcon::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        emit activated();
        break;
    default:
        break;
    }
}

void ClickableIcon::paintEvent(QPaintEvent* event) {
    if (!icons_loaded && !property("icon").toString().isEmpty()) {
        icons_loaded = true;

        QString name = property("icon").toString();
        icon = QIcon::fromTheme(name);
        icon_hover = QIcon::fromTheme(name + QStringLiteral("_hover"));
    }
    if (icons_loaded) {
        const bool highlight = hover || hasFocus();
        setPixmap((highlight ? icon_hover : icon).pixmap(size()));
    }

    QLabel::paintEvent(event);
}
