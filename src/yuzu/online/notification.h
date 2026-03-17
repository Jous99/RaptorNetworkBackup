// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono>

#include <QImage>
#include <QPixmap>
#include <QString>
#include <QWidget>

#include "yuzu/overlay.h"

class QMenuBar;
class QPropertyAnimation;
class QParallelAnimationGroup;
class QPaintEvent;
class QTimer;

class Notification : public Overlay {
    Q_OBJECT
    Q_PROPERTY(QImage image MEMBER image WRITE SetImage)
    Q_PROPERTY(QString title MEMBER title WRITE SetTitle)
    Q_PROPERTY(QString description MEMBER title WRITE SetDescription)
    Q_PROPERTY(qreal opacity MEMBER opacity WRITE SetOpacity)
    Q_PROPERTY(qreal position MEMBER position WRITE SetPosition)
    Q_PROPERTY(int active_time_ms MEMBER active_time_ms WRITE SetActiveTime)
    Q_PROPERTY(bool hide_right MEMBER hide_right WRITE SetHideRight)

public:
    explicit Notification(QWidget* parent = nullptr);
    ~Notification() override;

    void Play();

    [[nodiscard]] bool IsActive() const;

    void Reposition(QWidget* parent, QMenuBar* menubar) override;

    void SetImage(const QImage& image);
    void SetTitle(const QString& title);
    void SetDescription(const QString& description);
    void SetOpacity(qreal opacity);
    void SetPosition(qreal position);
    void SetActiveTime(int time_ms);
    void SetHideRight(bool hide_right);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPropertyAnimation* fadein_opacity_animation = nullptr;
    QPropertyAnimation* fadein_position_animation = nullptr;
    QParallelAnimationGroup* fadein_animation_group = nullptr;

    QPropertyAnimation* fadeout_opacity_animation = nullptr;
    QPropertyAnimation* fadeout_position_animation = nullptr;
    QParallelAnimationGroup* fadeout_animation_group = nullptr;

    QTimer* timer = nullptr;

    QImage image;
    QPixmap pixmap;
    QString title;
    QString description;
    qreal opacity = 1.0;
    qreal position = 0.0;
    int active_time_ms = 3000;
    bool hide_right = false;
};
