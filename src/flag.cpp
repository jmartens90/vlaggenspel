#include "flag.h"

#include <QJsonArray>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPixmap>
#include <QRandomGenerator>
#include <QtMath>

QString Emblem::shapeName(Shape s)
{
    switch (s) {
    case Circle: return QObject::tr("Circle");
    case Star: return QObject::tr("Star");
    case Cross: return QObject::tr("Cross");
    case Saltire: return QObject::tr("Diagonal cross");
    case HoistTriangle: return QObject::tr("Triangle");
    case Diamond: return QObject::tr("Diamond");
    case Square: return QObject::tr("Square");
    case Crescent: return QObject::tr("Moon");
    case Canton: return QObject::tr("Corner block");
    default: return QString();
    }
}

static QPolygonF starPolygon(QPointF c, double outer, int points = 5)
{
    QPolygonF poly;
    const double inner = outer * 0.382;
    for (int i = 0; i < points * 2; ++i) {
        const double r = (i % 2 == 0) ? outer : inner;
        const double a = -M_PI / 2 + i * M_PI / points;
        poly << QPointF(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
    }
    poly << poly.first();
    return poly;
}

QPainterPath Emblem::path(const QRectF &r) const
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    const QPointF c(r.left() + x * r.width(), r.top() + y * r.height());
    const double s = size * r.height();

    switch (shape) {
    case Circle:
        path.addEllipse(c, s / 2, s / 2);
        break;
    case Star:
        path.addPolygon(starPolygon(c, s / 2));
        break;
    case Cross: {
        const double t = s * 0.5;
        path.addRect(QRectF(r.left(), c.y() - t / 2, r.width(), t));
        path.addRect(QRectF(c.x() - t / 2, r.top(), t, r.height()));
        break;
    }
    case Saltire: {
        QPainterPath lines;
        lines.moveTo(r.topLeft());
        lines.lineTo(r.bottomRight());
        lines.moveTo(r.topRight());
        lines.lineTo(r.bottomLeft());
        QPainterPathStroker stroker;
        stroker.setWidth(s * 0.5);
        stroker.setCapStyle(Qt::FlatCap);
        path = stroker.createStroke(lines);
        path.setFillRule(Qt::WindingFill);
        break;
    }
    case HoistTriangle: {
        QPolygonF tri;
        tri << r.topLeft() << QPointF(r.left() + size * r.width(), c.y()) << r.bottomLeft() << r.topLeft();
        path.addPolygon(tri);
        break;
    }
    case Diamond: {
        QPolygonF d;
        d << QPointF(c.x(), c.y() - s / 2) << QPointF(c.x() + s * 0.8, c.y())
          << QPointF(c.x(), c.y() + s / 2) << QPointF(c.x() - s * 0.8, c.y()) << QPointF(c.x(), c.y() - s / 2);
        path.addPolygon(d);
        break;
    }
    case Square:
        path.addRect(QRectF(c.x() - s / 2, c.y() - s / 2, s, s));
        break;
    case Crescent: {
        QPainterPath outer;
        outer.addEllipse(c, s / 2, s / 2);
        QPainterPath inner;
        inner.addEllipse(QPointF(c.x() + s * 0.18, c.y()), s * 0.42, s * 0.42);
        path = outer.subtracted(inner);
        break;
    }
    case Canton:
        path.addRect(QRectF(r.left(), r.top(), size * r.width(), size * r.height() * 1.25));
        break;
    default:
        break;
    }
    return path.intersected(QPainterPath([&] { QPainterPath p; p.addRect(r); return p; }()));
}

QJsonObject Emblem::toJson() const
{
    return QJsonObject {
        { "shape", int(shape) },
        { "color", color.name() },
        { "x", x },
        { "y", y },
        { "size", size },
    };
}

Emblem Emblem::fromJson(const QJsonObject &o)
{
    Emblem e;
    e.shape = Shape(qBound(0, o.value("shape").toInt(), int(ShapeCount) - 1));
    e.color = QColor(o.value("color").toString("#ffffff"));
    e.x = o.value("x").toDouble(0.5);
    e.y = o.value("y").toDouble(0.5);
    e.size = o.value("size").toDouble(0.4);
    return e;
}

QRectF Flag::stripeRect(const QRectF &r, int i) const
{
    const int n = qMax(1, int(stripes.size()));
    if (layout == Horizontal) {
        const double h = r.height() / n;
        return QRectF(r.left(), r.top() + i * h, r.width(), h);
    }
    const double w = r.width() / n;
    return QRectF(r.left() + i * w, r.top(), w, r.height());
}

int Flag::stripeAt(const QRectF &r, const QPointF &p) const
{
    for (int i = 0; i < stripes.size(); ++i)
        if (stripeRect(r, i).contains(p))
            return i;
    return -1;
}

int Flag::emblemAt(const QRectF &r, const QPointF &p) const
{
    for (int i = emblems.size() - 1; i >= 0; --i)
        if (emblems[i].path(r).contains(p))
            return i;
    return -1;
}

void Flag::paint(QPainter &p, const QRectF &rect, bool outline) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < stripes.size(); ++i) {
        QRectF sr = stripeRect(rect, i);
        // Overlap a tiny bit so no hairline gaps appear between stripes.
        if (i + 1 < stripes.size())
            sr = layout == Horizontal ? sr.adjusted(0, 0, 0, 0.5) : sr.adjusted(0, 0, 0.5, 0);
        p.fillRect(sr, stripes[i]);
    }
    for (const Emblem &e : emblems) {
        p.setBrush(e.color);
        p.drawPath(e.path(rect));
    }
    if (outline) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 90), 1));
        p.drawRect(rect);
    }
    p.restore();
}

QPixmap Flag::toPixmap(int width) const
{
    const int height = qRound(width / kFlagAspect);
    QPixmap pm(width, height);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paint(p, QRectF(0.5, 0.5, width - 1, height - 1));
    return pm;
}

const QList<QColor> &Flag::palette()
{
    static const QList<QColor> colors {
        QColor("#d7263d"), QColor("#f46036"), QColor("#ffd23f"), QColor("#3bb273"),
        QColor("#0b6e4f"), QColor("#2e86ab"), QColor("#1b2a6b"), QColor("#7b2cbf"),
        QColor("#f5f5f5"), QColor("#111111"), QColor("#8d5524"), QColor("#ff8fab"),
    };
    return colors;
}

Flag Flag::random()
{
    auto *rng = QRandomGenerator::global();
    const auto &pal = palette();
    Flag f;
    f.layout = rng->bounded(2) ? Horizontal : Vertical;
    const int n = 1 + rng->bounded(3);
    f.stripes.clear();
    int last = -1;
    for (int i = 0; i < n; ++i) {
        int c;
        do { c = rng->bounded(int(pal.size())); } while (c == last);
        f.stripes << pal[c];
        last = c;
    }
    if (n == 1 || rng->bounded(2)) {
        Emblem e;
        e.shape = Emblem::Shape(rng->bounded(int(Emblem::ShapeCount)));
        e.color = pal[rng->bounded(int(pal.size()))];
        if (e.color == f.stripes.first())
            e.color = pal[(pal.indexOf(e.color) + 4) % pal.size()];
        e.size = e.shape == Emblem::HoistTriangle ? 0.4 : 0.45;
        if (e.shape == Emblem::Cross)
            e.x = 0.36, e.size = 0.4;
        if (e.shape == Emblem::Canton)
            e.size = 0.45;
        f.emblems << e;
    }
    return f;
}

QJsonObject Flag::toJson() const
{
    QJsonArray s;
    for (const QColor &c : stripes)
        s.append(c.name());
    QJsonArray e;
    for (const Emblem &em : emblems)
        e.append(em.toJson());
    return QJsonObject {
        { "layout", layout == Horizontal ? "horizontal" : "vertical" },
        { "stripes", s },
        { "emblems", e },
    };
}

Flag Flag::fromJson(const QJsonObject &o)
{
    Flag f;
    f.layout = o.value("layout").toString() == "vertical" ? Vertical : Horizontal;
    f.stripes.clear();
    for (const auto &v : o.value("stripes").toArray())
        f.stripes << QColor(v.toString());
    if (f.stripes.isEmpty())
        f.stripes << QColor(240, 240, 240);
    for (const auto &v : o.value("emblems").toArray())
        f.emblems << Emblem::fromJson(v.toObject());
    return f;
}
