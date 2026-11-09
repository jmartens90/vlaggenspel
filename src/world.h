#pragma once

#include "flag.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPainterPath>
#include <QPolygonF>
#include <QString>
#include <QVector>

struct Continent
{
    int id = 0;
    QString name;
    QColor color;
};

struct Country
{
    int id = 0;
    QString name;
    int continentId = 0;
    QColor color;
    Flag flag;
};

// A freehand line that divides land. Level 1 = continent border, level 2 = country border.
struct BorderStroke
{
    int level = 1;
    QPolygonF points;
};

// The whole invented world. It is a plain value type, so copying it is how undo works
// (all big members are implicitly shared, so copies are cheap until something changes).
class World
{
public:
    static constexpr int Width = 1600;
    static constexpr int Height = 1000;

    World();

    // ---- Step 1: land -------------------------------------------------------
    const QPainterPath &land() const { return m_land; }
    void addLand(const QPolygonF &outline);
    void eraseLand(const QPolygonF &outline);
    bool isLand(int x, int y) const;

    // ---- Steps 2 & 3: borders and regions -----------------------------------
    const QList<BorderStroke> &borders() const { return m_borders; }
    void addBorder(int level, const QPolygonF &points);
    // Removes the border of the given level closest to p (within maxDist). Returns true if one was removed.
    bool removeBorderNear(int level, const QPointF &p, double maxDist);

    int continentAt(int x, int y) const;
    int countryAt(int x, int y) const;

    // Flood-fills the area around (x, y) that is enclosed by coasts and borders, and gives it
    // to continent / country `id`. Returns the number of pixels changed (0 if not land).
    int fillContinent(int x, int y, int id);
    int fillCountry(int x, int y, int id);
    // True when every pixel of the area around (x, y) is still unassigned at that level.
    bool areaIsFree(int level, int x, int y) const;

    // ---- Continents & countries ---------------------------------------------
    QList<Continent> continents;
    QList<Country> countries;

    Continent *continent(int id);
    const Continent *continent(int id) const;
    Country *country(int id);
    const Country *country(int id) const;

    int newContinent();
    int newCountry(int continentId);
    void removeContinent(int id);
    void removeCountry(int id);

    // Where to write the name of a continent / country on the map (map coordinates).
    QHash<int, QPointF> continentLabelPositions() const;
    QHash<int, QPointF> countryLabelPositions() const;
    QHash<int, int> countryAreas() const;

    // ---- Rendering ----------------------------------------------------------
    enum ColorMode { LandOnly, ByContinent, ByCountry };
    QImage render(ColorMode mode, int selectedContinent, int selectedCountry) const;

    // ---- Files --------------------------------------------------------------
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &o, World &out, QString *error = nullptr);

    static QColor niceColor(int seed);

private:
    QVector<int> region(int level, int x, int y) const;
    void extendToCoast(int level, QPolygonF &poly, bool atEnd) const;
    void rebuildLandMask();
    void rebuildBorderMasks();
    void rebuildBorderMask(int level);
    void dropLabelsOutsideLand();

    QPainterPath m_land;
    QList<BorderStroke> m_borders;
    QVector<quint16> m_continentLabel; // 0 = none
    QVector<quint16> m_countryLabel;   // 0 = none
    int m_nextContinentId = 1;
    int m_nextCountryId = 1;

    // Caches rebuilt from the data above.
    QImage m_landMask;          // Grayscale8, 255 = land
    QImage m_continentBorders;  // Grayscale8, 255 = border
    QImage m_countryBorders;    // Grayscale8, 255 = border
};
