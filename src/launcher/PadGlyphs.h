// A controller's buttons as badges, the way consoles show them: a letter in a
// coloured disc (A, B, X, Y), PlayStation's shapes (✕ ○ □ △), and the rest
// (LB, Options, ...) as a small pill.
//
// They go into ordinary Text through the names buttonNamesFor() gives, each
// an inline image — <img src="image://pad/xbox:A" ...> — which Text shows as
// part of the line. So every hint that names a button ("%1 watch") shows the
// badge without being written any differently. The images come from
// PadGlyphProvider, which each QML engine registers as "pad".
//
// Keyboard keys are drawn the same way, as keycaps ("key:Enter"): QML asks
// PadGlyphs.key("Enter"), and Theme.hint() turns "[Enter] Open" into one.
#pragma once

#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QtQml/qqmlregistration.h>

// The badge for `id`, as markup for a Text: "xbox:A", "ps:cross", "plain:B",
// "pill:LB", "key:Enter", "key:up".
QString padGlyphMarkup(const QString& id);

// For QML: a keyboard key as a keycap, by its name as written ("Enter",
// "F5", "↑").
class PadGlyphs : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit PadGlyphs(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE QString key(const QString& name) const;
};

class PadGlyphProvider : public QQuickImageProvider {
public:
    PadGlyphProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};
