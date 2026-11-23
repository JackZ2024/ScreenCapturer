#pragma once

#include <QObject>

class QDialog;

class UpdatePrompt final : public QObject
{
    Q_OBJECT

public:
    static QDialog *create(const QString &version, const QString &notes = {});
    static bool wasSkipped(const QString &version);
    static void rememberSkipped(const QString &version);
};
