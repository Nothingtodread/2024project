/*
    SPDX-FileCopyrightText: 2008 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "ui_cliptranscode_ui.h"

#include <KMessageWidget>
#include <QUrl>

#include <QProcess>

class ClipTranscode : public QDialog, public Ui::ClipTranscode_UI
{
    Q_OBJECT

public:
    ClipTranscode(QStringList urls, const QString &params, QStringList postParams, const QString &description, QString folderInfo = QString(),
                  bool automaticMode = false, QWidget *parent = nullptr);
    ~ClipTranscode() override;

public Q_SLOTS:
    void slotStartTransCode();

private Q_SLOTS:
    void slotShowTranscodeInfo();
    void slotTranscodeFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void slotUpdateParams(int ix = -1);

private:
    QProcess m_transcodeProcess;
    QStringList m_urls;
    QString m_folderInfo;
    int m_duration;
    bool m_automaticMode;
    /** @brief The path for destination transcoded file. */
    QString m_destination;
    QStringList m_postParams;
    KMessageWidget *m_infoMessage;

Q_SIGNALS:
    void addClip(const QUrl &url, const QString &folderInfo = QString());
    void transcodedClip(const QUrl &source, const QUrl &result);
};
