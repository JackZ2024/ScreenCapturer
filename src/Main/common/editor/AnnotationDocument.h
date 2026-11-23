#ifndef ANNOTATION_DOCUMENT_H
#define ANNOTATION_DOCUMENT_H
#include <QObject>
#include "Annotation.h"
#include <QImage>
#include <QFont>
#include <QColor>
#include <QJsonArray>
#include <QTransform>
#include <QUndoStack>

class QPainter;
class AnnotationCommand;
class AnnotationDocument final : public QObject
{
    Q_OBJECT
public:
    explicit AnnotationDocument(QObject *parent = nullptr);
    void load(const QImage &base, const QJsonArray &annotations = {});
    const QImage &baseImage() const { return base_; }
    const QList<annotation::Annotation> &items() const { return items_; }
    QUndoStack *undoStack() { return &undo_; }
    QJsonArray serialize() const;
    QImage composite() const;
    static QImage render(const QImage &base, const QList<annotation::Annotation> &items);
    void add(annotation::Annotation annotation);
    void update(const annotation::Annotation &annotation);
    void remove(const QStringList &ids);
    void transformImage(const QTransform &transform);
    void grayscaleImage();
    static void paint(QPainter &painter, const annotation::Annotation &annotation);
signals:
    void changed();
private:
    friend class AnnotationCommand;
    void commit(QImage base, QList<annotation::Annotation> items, const QString &text);
    QImage base_;
    QList<annotation::Annotation> items_;
    QUndoStack undo_;
    mutable QImage composite_cache_;
};
#endif
