#include "InputMode.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QStringList>

#include "GamepadInput.h"
#include "KeyDelivery.h"

QVariantMap buttonNamesFor(const QString& kind) {
    // By position, the way the presses are read: south confirms, east goes
    // back, and so on — only the names differ. A Nintendo pad's south button
    // is B, which is why its row looks shuffled.
    QStringList names;
    if (kind == QLatin1String("playstation"))
        names = {QStringLiteral("✕"), QStringLiteral("○"), QStringLiteral("□"), QStringLiteral("△"),
                 QStringLiteral("L1"), QStringLiteral("R1"), QStringLiteral("Options"), QStringLiteral("PS")};
    else if (kind == QLatin1String("xbox"))
        names = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("X"), QStringLiteral("Y"),
                 QStringLiteral("LB"), QStringLiteral("RB"), QStringLiteral("Menu"), QStringLiteral("Xbox")};
    else if (kind == QLatin1String("nintendo"))
        names = {QStringLiteral("B"), QStringLiteral("A"), QStringLiteral("Y"), QStringLiteral("X"),
                 QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("+"), QStringLiteral("Home")};
    else
        names = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("X"), QStringLiteral("Y"),
                 QStringLiteral("L1"), QStringLiteral("R1"), QStringLiteral("Start"), QStringLiteral("Guide")};
    const QStringList roles = {QStringLiteral("south"), QStringLiteral("east"), QStringLiteral("west"),
                               QStringLiteral("north"), QStringLiteral("l1"), QStringLiteral("r1"),
                               QStringLiteral("start"), QStringLiteral("guide")};
    QVariantMap map;
    for (int i = 0; i < roles.size(); ++i) map.insert(roles.at(i), names.at(i));
    return map;
}

InputMode::InputMode(GamepadInput* pad, QObject* parent) : QObject(parent), pad_(pad) {
    connect(pad_, &GamepadInput::keyPressed, this, [this](int) { setUsingController(true); });
    connect(pad_, &GamepadInput::kindChanged, this, &InputMode::buttonNamesChanged);
    QCoreApplication::instance()->installEventFilter(this);
}

QVariantMap InputMode::buttonNames() const { return buttonNamesFor(pad_->kind()); }

void InputMode::setUsingController(bool on) {
    if (usingController_ == on) return;
    usingController_ = on;
    emit usingControllerChanged();
}

bool InputMode::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
        case QEvent::KeyPress:
            if (static_cast<QKeyEvent*>(event)->nativeScanCode() != kControllerScanCode)
                setUsingController(false);
            break;
        case QEvent::MouseButtonPress:
        case QEvent::TouchBegin:
            setUsingController(false);
            break;
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}
