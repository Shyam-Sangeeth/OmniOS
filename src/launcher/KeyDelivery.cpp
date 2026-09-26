#include "KeyDelivery.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QWindow>

void deliverKey(int key, const std::function<void()>& wantFocus) {
    if (wantFocus) wantFocus();

    QWindow* window = QGuiApplication::focusWindow();
    if (window == nullptr) {
        const QWindowList windows = QGuiApplication::topLevelWindows();
        for (QWindow* candidate : windows) {
            if (candidate != nullptr && candidate->isVisible()) {
                window = candidate;
                break;
            }
        }
    }
    if (window == nullptr) return;

    // A press and a release, because Qt's key handling expects both and some
    // handlers act on release.
    QGuiApplication::postEvent(window, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
    QGuiApplication::postEvent(window, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
}
