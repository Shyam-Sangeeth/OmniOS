#include "PadGlyphs.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

// How tall a badge is in the line, in logical pixels, and how many times that
// it is drawn at, so it stays sharp on a high-density screen once scaled.
constexpr int kHeight = 18;
constexpr int kOversample = 3;

const QColor kDisc("#2E2A3E");
const QColor kRim("#59FFFFFF");

QFont labelFont(qreal height, bool pill) {
    QFont font;
    font.setPixelSize(qRound(height * (pill ? 0.56 : 0.64)));
    font.setBold(true);
    return font;
}

// The colour a face button is marked in, on the pad itself.
QColor faceColour(const QString& kind, const QString& name) {
    if (kind == QLatin1String("xbox")) {
        if (name == QLatin1String("A")) return QColor("#7EC845");
        if (name == QLatin1String("B")) return QColor("#F25F5C");
        if (name == QLatin1String("X")) return QColor("#4AA3F0");
        if (name == QLatin1String("Y")) return QColor("#F6C343");
    } else if (kind == QLatin1String("ps")) {
        if (name == QLatin1String("cross")) return QColor("#8DB9F2");
        if (name == QLatin1String("circle")) return QColor("#F27C7C");
        if (name == QLatin1String("square")) return QColor("#E79BD0");
        if (name == QLatin1String("triangle")) return QColor("#4FD1B0");
    }
    return QColor("#F0F0F5");
}

bool isBox(const QString& kind) { return kind == QLatin1String("pill") || kind == QLatin1String("key"); }

// Arrows travel by name, so an image URL stays plain ASCII.
const QString kArrows[4][2] = {{QStringLiteral("up"), QStringLiteral("↑")},
                               {QStringLiteral("down"), QStringLiteral("↓")},
                               {QStringLiteral("left"), QStringLiteral("←")},
                               {QStringLiteral("right"), QStringLiteral("→")}};

// What the badge says: an arrow's name as the arrow, anything else as it is.
QString label(const QString& kind, const QString& name) {
    if (kind == QLatin1String("key"))
        for (const auto& arrow : kArrows)
            if (name == arrow[0]) return arrow[1];
    return name;
}

// Width for height `h`: a disc is square; a pill or a key is as wide as its
// label, and never narrower than it is tall.
qreal widthFor(const QString& kind, const QString& name, qreal h) {
    if (!isBox(kind)) return h;
    const QFontMetricsF metrics(labelFont(h, true));
    return std::max(h, metrics.horizontalAdvance(label(kind, name)) + h * 0.7);
}

}  // namespace

QString PadGlyphs::key(const QString& name) const {
    QString id = name;
    for (const auto& arrow : kArrows)
        if (name == arrow[1]) id = arrow[0];
    return padGlyphMarkup(QStringLiteral("key:") + id);
}

QString padGlyphMarkup(const QString& id) {
    const QString kind = id.section(QLatin1Char(':'), 0, 0);
    const QString name = id.section(QLatin1Char(':'), 1);
    const int width = qRound(widthFor(kind, name, kHeight));
    return QStringLiteral("<img src=\"image://pad/%1\" width=\"%2\" height=\"%3\" align=\"middle\">")
        .arg(id)  // letters, ":" and "+" only, which a URL keeps as they are
        .arg(width)
        .arg(kHeight);
}

QImage PadGlyphProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const QString kind = id.section(QLatin1Char(':'), 0, 0);
    const QString name = id.section(QLatin1Char(':'), 1);
    const qreal h = requestedSize.height() > 0 ? std::max(requestedSize.height(), kHeight * kOversample)
                                               : kHeight * kOversample;
    const qreal w = widthFor(kind, name, h);

    QImage image(QSize(qCeil(w), qCeil(h)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal rim = std::max(1.0, h * 0.06);
    const QRectF body(rim / 2, rim / 2, w - rim, h - rim);
    painter.setPen(QPen(kRim, rim));
    painter.setBrush(kDisc);
    if (kind == QLatin1String("key")) {
        // A keycap: squarer than a pill, with a deeper bottom edge, as a key
        // seen from a little above.
        const qreal depth = h * 0.1;
        painter.setBrush(QColor("#1C1A26"));
        painter.drawRoundedRect(body, h * 0.2, h * 0.2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(kDisc);
        painter.drawRoundedRect(body.adjusted(rim, rim, -rim, -rim - depth), h * 0.15, h * 0.15);
    } else if (kind == QLatin1String("pill")) {
        painter.drawRoundedRect(body, h * 0.3, h * 0.3);
    } else {
        painter.drawEllipse(body);
    }

    const QColor colour = faceColour(kind, name);
    if (kind == QLatin1String("ps")) {
        // The shapes, drawn rather than typed: fonts draw ✕ ○ □ △ at all
        // sizes and weights, and rarely centred.
        const qreal stroke = h * 0.09;
        painter.setPen(QPen(colour, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QPointF c(w / 2, h / 2);
        const qreal r = h * 0.22;
        if (name == QLatin1String("cross")) {
            painter.drawLine(c + QPointF(-r, -r), c + QPointF(r, r));
            painter.drawLine(c + QPointF(-r, r), c + QPointF(r, -r));
        } else if (name == QLatin1String("circle")) {
            painter.drawEllipse(c, r * 1.1, r * 1.1);
        } else if (name == QLatin1String("square")) {
            painter.drawRect(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r));
        } else {
            QPainterPath triangle;
            const qreal t = r * 1.2;
            triangle.moveTo(c.x(), c.y() - t);
            triangle.lineTo(c.x() + t * std::sqrt(3.0) / 2, c.y() + t / 2);
            triangle.lineTo(c.x() - t * std::sqrt(3.0) / 2, c.y() + t / 2);
            triangle.closeSubpath();
            painter.translate(0, h * 0.04);  // its middle is low; lifted to look centred
            painter.drawPath(triangle);
        }
    } else {
        painter.setPen(colour);
        painter.setFont(labelFont(h, isBox(kind)));
        // A key's label sits on its top face, above the bottom edge.
        const qreal lift = kind == QLatin1String("key") ? h * 0.05 : 0;
        painter.drawText(QRectF(0, -lift, w, h), Qt::AlignCenter, label(kind, name));
    }
    painter.end();

    if (size) *size = image.size();
    return image;
}
