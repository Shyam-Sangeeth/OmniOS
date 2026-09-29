#include "ControlsOverlay.h"

#include <QColor>
#include <QGuiApplication>
#include <QMargins>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QtMath>

#ifdef OMNIOS_HAS_LAYERSHELL
#include <LayerShellQt/Window>
#endif

ControlsOverlay::ControlsOverlay(QQmlEngine* engine, QObject* parent) : QObject(parent) {
    view_ = new QQuickView(engine, nullptr);
    view_->setColor(Qt::transparent);
    // The bar is as wide as the screen and as tall as its lines need.
    view_->setResizeMode(QQuickView::SizeRootObjectToView);
    // Never the focus, never in the way of the pointer, never on the taskbar.
    view_->setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput |
                    Qt::WindowStaysOnTopHint | Qt::Tool);
    view_->setTitle(QStringLiteral("OmniOS keyboard controls"));

#ifdef OMNIOS_HAS_LAYERSHELL
    // Before the window exists: the layer is chosen as its surface is made.
    if (LayerShellQt::Window* layer = LayerShellQt::Window::get(view_)) {
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom |
                                                         LayerShellQt::Window::AnchorLeft |
                                                         LayerShellQt::Window::AnchorRight));
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        // At the screen's edge, over Plasma's panel's space rather than above
        // it: the game is full screen, and the panel is under it.
        layer->setExclusiveZone(-1);
        layer->setScope(QStringLiteral("omnios-controls"));
    }
#endif

    view_->loadFromModule("omnios", "ControlsOverlay");
    if (QQuickItem* root = view_->rootObject())
        connect(root, &QQuickItem::implicitHeightChanged, this, &ControlsOverlay::fitHeight);
    else
        qWarning("omni-launcher: the keyboard controls bar did not load");

    // The library in front again (Guide, Meta+Esc, the game ending) puts the
    // bar away; Resume from the keyboard brings it back.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) hide();
    });
}

ControlsOverlay::~ControlsOverlay() { delete view_; }

void ControlsOverlay::fitHeight() {
    if (QQuickItem* root = view_->rootObject()) view_->setHeight(qCeil(root->implicitHeight()));
}

void ControlsOverlay::show(const QString& title, const QVariantList& rows) {
    QQuickItem* root = view_->rootObject();
    if (root == nullptr || rows.isEmpty()) {
        hide();
        return;
    }
    if (QScreen* screen = view_->screen() ? view_->screen() : QGuiApplication::primaryScreen())
        view_->setWidth(screen->geometry().width());
    root->setProperty("rows", rows);
    fitHeight();
    qInfo("keyboard controls: shown for %s", qPrintable(title));
    view_->show();
}

void ControlsOverlay::hide() {
    if (view_->isVisible()) qInfo("keyboard controls: hidden");
    view_->hide();
}
