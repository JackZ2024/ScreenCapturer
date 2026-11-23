#include "ImageSaveFormat.h"
#include <QDir>
#include <QFileInfo>

QString image_save::filters()
{
    return "PNG(*.png);;JPEG(*.jpg *.jpeg);;BMP(*.bmp)";
}

QString image_save::suffixForFilter(const QString &filter, const QString &preferredSuffix)
{
    if (filter == "JPEG(*.jpg *.jpeg)") return preferredSuffix == "jpeg" ? "jpeg" : "jpg";
    return filter == "BMP(*.bmp)" ? "bmp" : "png";
}

ImageSaveTarget image_save::target(const QString &filename, const QString &filter, const QString &preferredSuffix)
{
    if (filename.isEmpty()) return {};
    ImageSaveTarget result;
    result.filter = filter;
    if (filter == "JPEG(*.jpg *.jpeg)") result.format = "JPEG";
    else if (filter == "BMP(*.bmp)") result.format = "BMP";
    else {
        result.filter = "PNG(*.png)";
        result.format = "PNG";
    }
    result.suffix = suffixForFilter(result.filter, preferredSuffix);
    const QFileInfo info(filename);
    const QString typedSuffix = info.suffix().toLower();
    // Preserve either JPEG spelling when the name already matches the selected type.
    if (result.format == "JPEG" && (typedSuffix == "jpg" || typedSuffix == "jpeg")) result.suffix = typedSuffix;
    const bool matches = typedSuffix == result.suffix;
    if (matches) {
        result.filename = filename;
    } else {
        QString base = info.fileName();
        if (!info.suffix().isEmpty()) base.chop(info.suffix().size() + 1);
        while (base.endsWith('.')) base.chop(1);
        if (base.isEmpty()) base = "S";
        result.filename = info.dir().filePath(base + '.' + result.suffix);
    }
    return result;
}
