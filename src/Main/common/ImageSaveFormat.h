#ifndef CAPTURER_IMAGE_SAVE_FORMAT_H
#define CAPTURER_IMAGE_SAVE_FORMAT_H
#include <QByteArray>
#include <QString>

struct ImageSaveTarget
{
    QString filename;
    QString filter;
    QString suffix;
    QByteArray format;
    bool isEmpty() const { return filename.isEmpty(); }
};

namespace image_save
{
    QString filters();
    QString suffixForFilter(const QString &filter, const QString &preferredSuffix);
    ImageSaveTarget target(const QString &filename, const QString &filter, const QString &preferredSuffix);
}
#endif
