// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>

#include <QMenuBar>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

#include "yuzu/online/notification.h"

using namespace std::chrono_literals;

constexpr QSize NOTIFICATION_SIZE = QSize(352, 80);
constexpr std::chrono::milliseconds FADEOUT_TIME = 300ms;
constexpr std::chrono::milliseconds FADEIN_TIME = 100ms;

Notification::Notification(QWidget* parent) : Overlay(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    resize(NOTIFICATION_SIZE);

    fadein_opacity_animation = new QPropertyAnimation(this, "opacity");
    fadein_opacity_animation->setDuration(FADEOUT_TIME.count());
    fadein_opacity_animation->setKeyValueAt(0.0, qreal(0.0));
    fadein_opacity_animation->setKeyValueAt(0.1, qreal(0.7));
    fadein_opacity_animation->setKeyValueAt(1.0, qreal(1.0));

    fadein_position_animation = new QPropertyAnimation(this, "position");
    fadein_position_animation->setDuration(FADEIN_TIME.count());
    fadein_position_animation->setKeyValueAt(0.0, 1.0);
    fadein_position_animation->setKeyValueAt(0.3, 0.8);
    fadein_position_animation->setKeyValueAt(1.0, 0.0);

    fadein_animation_group = new QParallelAnimationGroup(this);
    fadein_animation_group->addAnimation(fadein_opacity_animation);
    fadein_animation_group->addAnimation(fadein_position_animation);

    fadeout_opacity_animation = new QPropertyAnimation(this, "opacity");
    fadeout_opacity_animation->setDuration(FADEOUT_TIME.count());
    fadeout_opacity_animation->setKeyValueAt(0.0, qreal(1.0));
    fadeout_opacity_animation->setKeyValueAt(0.1, qreal(0.5));
    fadeout_opacity_animation->setKeyValueAt(1.0, qreal(0.0));

    fadeout_position_animation = new QPropertyAnimation(this, "position");
    fadeout_position_animation->setDuration(FADEOUT_TIME.count());
    fadeout_position_animation->setKeyValueAt(0.0, 0.0);
    fadeout_position_animation->setKeyValueAt(1.0, 1.0);

    fadeout_animation_group = new QParallelAnimationGroup(this);
    fadeout_animation_group->addAnimation(fadeout_opacity_animation);
    fadeout_animation_group->addAnimation(fadeout_position_animation);

    timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this] { fadeout_animation_group->start(); });
}

Notification::~Notification() = default;

void Notification::Play() {
    timer->setInterval(active_time_ms);
    timer->start();

    fadein_animation_group->start();
}

bool Notification::IsActive() const {
    return timer->isActive();
}

void Notification::Reposition(QWidget* parent, QMenuBar* menubar) {
    if (hide_right) {
        move(parent->size().width() - width(), menubar->height());
    } else {
        move(0, menubar->height());
    }
}

void Notification::SetImage(const QImage& image_) {
    static constexpr QSize ICON_SIZE(QSize(NOTIFICATION_SIZE.height(), NOTIFICATION_SIZE.height()));
    image = image_;
    if (image.isNull()) {
        pixmap = QPixmap{};
    } else {
        pixmap = QPixmap::fromImage(image).scaled(ICON_SIZE, Qt::AspectRatioMode::KeepAspectRatio,
                                                  Qt::TransformationMode::SmoothTransformation);
    }
    repaint();
}

void Notification::SetTitle(const QString& title_) {
    title = title_;
    repaint();
}

void Notification::SetDescription(const QString& description_) {
    description = description_;
    repaint();
}

void Notification::SetOpacity(qreal opacity_) {
    opacity = opacity_;
    repaint();
}

void Notification::SetPosition(qreal position_) {
    position = position_;
    repaint();
}

void Notification::SetActiveTime(int time_ms) {
    active_time_ms = time_ms;
}

void Notification::SetHideRight(bool hide_right_) {
    hide_right = hide_right_;
}

void Notification::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setOpacity(opacity);

    painter.translate(width() * (hide_right ? position : -position), 0);

    QRect area = rect();
    painter.fillRect(area, QColor(50, 50, 50, 192));

    QRect border_area = area;
    border_area.setWidth(8);
    painter.fillRect(border_area, QColor(86, 214, 51, 192));

    painter.drawPixmap(8, 0, pixmap);

    QFont font = painter.font();
    font.setPixelSize(12);
    painter.setFont(font);
    painter.setPen(QPen(QColor(255, 255, 255)));

    QRect description_area = area;
    description_area.setTop(50);
    description_area.setLeft(107);
    painter.drawText(description_area, description, Qt::AlignTop | Qt::AlignLeft);

    font.setBold(true);
    font.setPixelSize(18);
    painter.setFont(font);

    QRect title_area = area;
    title_area.setTop(18);
    title_area.setLeft(107);
    painter.drawText(title_area, title, Qt::AlignTop | Qt::AlignLeft);
}
