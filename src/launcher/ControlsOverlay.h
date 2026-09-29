// The keyboard's controls, as a bar along the bottom of a game played from
// the keyboard: which key is which of the console's buttons
// (omnios::keyboardControls). It stays for as long as the game has the front,
// goes when the library is back or a pad is used, and comes back on Resume.
//
// A game plays full screen, and Plasma keeps a full-screen window above every
// ordinary one, "keep above" included. So this is a layer-shell surface on the
// overlay layer, which KWin draws above full screen: through LayerShellQt,
// Plasma's own library for it. It takes no keyboard focus — the game keeps
// every key — and lets the pointer through. Built without LayerShellQt it is
// an ordinary always-on-top window, which a full-screen game covers.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class QQmlEngine;
class QQuickView;

class ControlsOverlay : public QObject {
    Q_OBJECT
public:
    // `engine` is the launcher's, for its image provider (the keycaps) and
    // its QML module.
    explicit ControlsOverlay(QQmlEngine* engine, QObject* parent = nullptr);
    ~ControlsOverlay() override;

public slots:
    // Shows `rows` ([{keys: [...], button: "..."}]) for the game `title` (for
    // the log). Nothing to show hides it.
    void show(const QString& title, const QVariantList& rows);
    void hide();

private:
    void fitHeight();

    QQuickView* view_ = nullptr;
};
