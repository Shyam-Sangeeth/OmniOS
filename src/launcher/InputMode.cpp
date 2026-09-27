#include "InputMode.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QStringList>

#include "GamepadInput.h"
#include "KeyDelivery.h"
#include "PadGlyphs.h"

QVariantMap buttonNamesFor(const QString& kind) {
    // By position, the way the presses are read: south confirms, east goes
    // back, and so on — only the names differ. A Nintendo pad's south button
    // is B, which is why its row looks shuffled.
    //
    // Each is a badge (see PadGlyphs.h), shown inline wherever a hint names it.
    QStringList names;
    if (kind == QLatin1String("playstation"))
        names = {QStringLiteral("ps:cross"), QStringLiteral("ps:circle"), QStringLiteral("ps:square"),
                 QStringLiteral("ps:triangle"), QStringLiteral("pill:L1"), QStringLiteral("pill:R1"),
                 QStringLiteral("pill:Options"), QStringLiteral("pill:PS")};
    else if (kind == QLatin1String("xbox"))
        names = {QStringLiteral("xbox:A"), QStringLiteral("xbox:B"), QStringLiteral("xbox:X"),
                 QStringLiteral("xbox:Y"), QStringLiteral("pill:LB"), QStringLiteral("pill:RB"),
                 QStringLiteral("pill:Menu"), QStringLiteral("pill:Xbox")};
    else if (kind == QLatin1String("nintendo"))
        names = {QStringLiteral("plain:B"), QStringLiteral("plain:A"), QStringLiteral("plain:Y"),
                 QStringLiteral("plain:X"), QStringLiteral("pill:L"), QStringLiteral("pill:R"),
                 QStringLiteral("pill:+"), QStringLiteral("pill:Home")};
    else
        names = {QStringLiteral("plain:A"), QStringLiteral("plain:B"), QStringLiteral("plain:X"),
                 QStringLiteral("plain:Y"), QStringLiteral("pill:L1"), QStringLiteral("pill:R1"),
                 QStringLiteral("pill:Start"), QStringLiteral("pill:Guide")};
    const QStringList roles = {QStringLiteral("south"), QStringLiteral("east"), QStringLiteral("west"),
                               QStringLiteral("north"), QStringLiteral("l1"), QStringLiteral("r1"),
                               QStringLiteral("start"), QStringLiteral("guide")};
    QVariantMap map;
    for (int i = 0; i < roles.size(); ++i) map.insert(roles.at(i), padGlyphMarkup(names.at(i)));
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
