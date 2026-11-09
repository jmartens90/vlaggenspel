#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QRectF>
#include <QString>

class QPainter;

// A shape placed on top of the flag's stripes.
struct Emblem
{
    enum Shape {
        Circle,
        Star,
        Cross,
        Saltire,
        HoistTriangle,
        Diamond,
        Square,
        Crescent,
        Canton,
        ShapeCount
    };

    Shape shape = Circle;
    QColor color = Qt::white;
    double x = 0.5;    // centre, as fraction of flag width
    double y = 0.5;    // centre, as fraction of flag height
    double size = 0.4; // as fraction of flag height (or width for the triangle/canton)

    static QString shapeName(Shape s);
    QPainterPath path(const QRectF &flagRect) const;

    QJsonObject toJson() const;
    static Emblem fromJson(const QJsonObject &o);
};

struct Flag
{
    enum Layout { Horizontal, Vertical };

    Layout layout = Horizontal;
    QList<QColor> stripes { QColor(240, 240, 240) };
    QList<Emblem> emblems;

    // Index of the stripe under a point, or -1.
    int stripeAt(const QRectF &flagRect, const QPointF &p) const;
    QRectF stripeRect(const QRectF &flagRect, int i) const;
    // Index of the top-most emblem under a point, or -1.
    int emblemAt(const QRectF &flagRect, const QPointF &p) const;

    void paint(QPainter &p, const QRectF &rect, bool outline = true) const;
    QPixmap toPixmap(int width) const;

    static Flag random();
    static const QList<QColor> &palette();

    QJsonObject toJson() const;
    static Flag fromJson(const QJsonObject &o);
};

// Width / height of every flag in the game.
constexpr double kFlagAspect = 1.5;
