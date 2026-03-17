// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QIcon>
#include <fmt/format.h>
#include "common/scm_rev.h"
#include "ui_aboutdialog.h"
#include "yuzu/about_dialog.h"

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent), ui(new Ui::AboutDialog) {
    const auto yuzu_build_version = std::string(Common::g_build_string);

    ui->setupUi(this);
    ui->labelLogo->setPixmap(QIcon::fromTheme(QStringLiteral("citrus")).pixmap(200));
    ui->labelBuildInfo->setText(ui->labelBuildInfo->text().arg(
        QString::fromStdString(yuzu_build_version), QString{}, QString{}, QString{}.left(10)));
    ui->labelLinks->setText(
        QStringLiteral("<a href=\"https://raptor.network\">Website</a> | <a "
                       "href=\"https://sentinel.raptor.network/v1/package/%1\">Source Code</a> | "
                       "<a href=\"https://raptor.network/citrus/license\">License</a>")
            .arg(QString::fromLatin1(Common::g_link_source_package_id)));
}

AboutDialog::~AboutDialog() = default;
