#include "world.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <limits>

namespace {
constexpr int kContinentBorderWidth = 4;
constexpr int kCountryBorderWidth = 3;
const QColor kLandColor(232, 220, 181);

QJsonArray polygonToJson(const QPolygonF &poly)
{
    QJsonArray a;
    for (const QPointF &p : poly) {
        a.append(qRound(p.x() * 10) / 10.0);
        a.append(qRound(p.y() * 10) / 10.0);
    }
    return a;
}

QPolygonF polygonFromJson(const QJsonArray &a)
{
    QPolygonF poly;
    for (int i = 0; i + 1 < a.size(); i += 2)
        poly << QPointF(a[i].toDouble(), a[i + 1].toDouble());
    return poly;
}

QString labelsToString(const QVector<quint16> &labels)
{
    const QByteArray raw(reinterpret_cast<const char *>(labels.constData()), labels.size() * int(sizeof(quint16)));
    return QString::fromLatin1(qCompress(raw, 9).toBase64());
}

bool labelsFromString(const QString &s, QVector<quint16> &labels)
{
    const QByteArray raw = qUncompress(QByteArray::fromBase64(s.toLatin1()));
    if (raw.size() != World::Width * World::Height * int(sizeof(quint16)))
        return false;
    labels.resize(World::Width * World::Height);
    memcpy(labels.data(), raw.constData(), size_t(raw.size()));
    return true;
}

double distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    double t = len2 > 0 ? QPointF::dotProduct(p - a, ab) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF d = p - (a + t * ab);
    return std::sqrt(QPointF::dotProduct(d, d));
}
} // namespace

World::World()
    : m_continentLabel(Width * Height, 0)
    , m_countryLabel(Width * Height, 0)
{
    m_land.setFillRule(Qt::WindingFill);
    rebuildLandMask();
    rebuildBorderMasks();
}

QColor World::niceColor(int seed)
{
    // Golden-angle hue steps give colours that are spread out and pleasant next to each other.
    const int hue = int(std::fmod(seed * 137.508, 360.0));
    const int sat = 110 + (seed * 53) % 70;
    const int val = 200 + (seed * 31) % 50;
    return QColor::fromHsv(hue, sat, val);
}

// ---- Land -------------------------------------------------------------------

void World::addLand(const QPolygonF &outline)
{
    QPainterPath p;
    p.addPolygon(outline);
    p.closeSubpath();
    m_land = m_land.united(p).simplified();
    rebuildLandMask();
}

void World::eraseLand(const QPolygonF &outline)
{
    QPainterPath p;
    p.addPolygon(outline);
    p.closeSubpath();
    m_land = m_land.subtracted(p).simplified();
    rebuildLandMask();
    dropLabelsOutsideLand();
}

bool World::isLand(int x, int y) const
{
    if (x < 0 || y < 0 || x >= Width || y >= Height)
        return false;
    return m_landMask.constScanLine(y)[x] != 0;
}

void World::rebuildLandMask()
{
    m_landMask = QImage(Width, Height, QImage::Format_Grayscale8);
    m_landMask.fill(0);
    QPainter p(&m_landMask);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillPath(m_land, QColor(255, 255, 255));
}

void World::dropLabelsOutsideLand()
{
    for (int y = 0; y < Height; ++y) {
        const uchar *land = m_landMask.constScanLine(y);
        for (int x = 0; x < Width; ++x) {
            if (!land[x]) {
                const int i = y * Width + x;
                if (m_continentLabel[i])
                    m_continentLabel[i] = 0;
                if (m_countryLabel[i])
                    m_countryLabel[i] = 0;
            }
        }
    }
}

// ---- Borders ----------------------------------------------------------------

// Lines drawn by hand rarely reach the coast exactly. If an end of the line stops on land,
// stretch it straight on (up to kMaxReach pixels) until it meets the sea or another border.
void World::extendToCoast(int level, QPolygonF &poly, bool atEnd) const
{
    constexpr int kMaxReach = 80;
    const QPointF p = atEnd ? poly.last() : poly.first();
    if (!isLand(int(p.x()), int(p.y())))
        return;

    // Direction of the line near its end, measured over ~10 pixels.
    QPointF q = p;
    for (int k = 1; k < poly.size(); ++k) {
        q = atEnd ? poly[poly.size() - 1 - k] : poly[k];
        if (QLineF(p, q).length() >= 10)
            break;
    }
    QLineF dir(q, p);
    if (dir.length() < 1)
        return;
    dir.setLength(1);
    const QPointF step = dir.p2() - dir.p1();

    auto onBorder = [&](int x, int y) {
        if (m_continentBorders.constScanLine(y)[x])
            return true;
        return level == 2 && m_countryBorders.constScanLine(y)[x];
    };
    if (onBorder(int(p.x()), int(p.y())))
        return;
    for (int i = 1; i <= kMaxReach; ++i) {
        const QPointF t = p + step * i;
        const int x = int(t.x()), y = int(t.y());
        if (!isLand(x, y) || onBorder(x, y)) {
            const QPointF end = p + step * (i + 2);
            if (atEnd)
                poly.append(end);
            else
                poly.prepend(end);
            return;
        }
    }
}

void World::addBorder(int level, const QPolygonF &points)
{
    if (points.size() < 2)
        return;
    QPolygonF poly = points;
    extendToCoast(level, poly, false);
    extendToCoast(level, poly, true);
    m_borders.append({ level, poly });
    rebuildBorderMask(level);
}

bool World::removeBorderNear(int level, const QPointF &p, double maxDist)
{
    int best = -1;
    double bestDist = maxDist;
    for (int i = 0; i < m_borders.size(); ++i) {
        const BorderStroke &b = m_borders[i];
        if (b.level != level)
            continue;
        for (int j = 0; j + 1 < b.points.size(); ++j) {
            const double d = distanceToSegment(p, b.points[j], b.points[j + 1]);
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
    }
    if (best < 0)
        return false;
    m_borders.removeAt(best);
    rebuildBorderMask(level);
    return true;
}

void World::rebuildBorderMasks()
{
    rebuildBorderMask(1);
    rebuildBorderMask(2);
}

void World::rebuildBorderMask(int level)
{
    QImage &mask = level == 1 ? m_continentBorders : m_countryBorders;
    mask = QImage(Width, Height, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter p(&mask);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(255, 255, 255), level == 1 ? kContinentBorderWidth : kCountryBorderWidth,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const BorderStroke &b : m_borders)
        if (b.level == level)
            p.drawPolyline(b.points);
}

// ---- Regions ----------------------------------------------------------------

// Pixels reachable from (x, y) without crossing sea or a border of this level (or above).
// For countries the fill also stays inside the continent it started in.
QVector<int> World::region(int level, int x, int y) const
{
    QVector<int> out;
    if (!isLand(x, y))
        return out;

    auto blocked = [&](int px, int py) {
        if (m_continentBorders.constScanLine(py)[px])
            return true;
        if (level == 2 && m_countryBorders.constScanLine(py)[px])
            return true;
        return false;
    };
    if (blocked(x, y))
        return out;

    const int startContinent = m_continentLabel[y * Width + x];
    QVector<uchar> seen(Width * Height, 0);
    QVector<int> stack;
    stack.reserve(4096);
    stack.push_back(y * Width + x);
    seen[y * Width + x] = 1;

    while (!stack.isEmpty()) {
        const int i = stack.takeLast();
        out.push_back(i);
        const int cx = i % Width;
        const int cy = i / Width;
        const int nx[4] = { cx + 1, cx - 1, cx, cx };
        const int ny[4] = { cy, cy, cy + 1, cy - 1 };
        for (int k = 0; k < 4; ++k) {
            const int px = nx[k], py = ny[k];
            if (px < 0 || py < 0 || px >= Width || py >= Height)
                continue;
            const int j = py * Width + px;
            if (seen[j])
                continue;
            seen[j] = 1;
            if (!m_landMask.constScanLine(py)[px] || blocked(px, py))
                continue;
            if (level == 2 && m_continentLabel[j] != startContinent)
                continue;
            stack.push_back(j);
        }
    }
    return out;
}

bool World::areaIsFree(int level, int x, int y) const
{
    const QVector<quint16> &labels = level == 1 ? m_continentLabel : m_countryLabel;
    for (int i : region(level, x, y))
        if (labels[i])
            return false;
    return true;
}

int World::fillContinent(int x, int y, int id)
{
    const QVector<int> pixels = region(1, x, y);
    for (int i : pixels)
        m_continentLabel[i] = quint16(id);
    return pixels.size();
}

int World::fillCountry(int x, int y, int id)
{
    const QVector<int> pixels = region(2, x, y);
    for (int i : pixels)
        m_countryLabel[i] = quint16(id);
    if (Country *c = country(id); c && !c->continentId && !pixels.isEmpty())
        c->continentId = m_continentLabel[pixels.first()];
    return pixels.size();
}

int World::continentAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= Width || y >= Height)
        return 0;
    return m_continentLabel[y * Width + x];
}

int World::countryAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= Width || y >= Height)
        return 0;
    return m_countryLabel[y * Width + x];
}

// ---- Continents & countries -------------------------------------------------

Continent *World::continent(int id)
{
    for (Continent &c : continents)
        if (c.id == id)
            return &c;
    return nullptr;
}

const Continent *World::continent(int id) const
{
    return const_cast<World *>(this)->continent(id);
}

Country *World::country(int id)
{
    for (Country &c : countries)
        if (c.id == id)
            return &c;
    return nullptr;
}

const Country *World::country(int id) const
{
    return const_cast<World *>(this)->country(id);
}

int World::newContinent()
{
    Continent c;
    c.id = m_nextContinentId++;
    c.name = QObject::tr("Continent %1").arg(c.id);
    c.color = niceColor(c.id * 3 + 1);
    continents.append(c);
    return c.id;
}

int World::newCountry(int continentId)
{
    Country c;
    c.id = m_nextCountryId++;
    c.name = QObject::tr("Country %1").arg(c.id);
    c.color = niceColor(c.id);
    c.continentId = continentId;
    c.flag = Flag::random();
    countries.append(c);
    return c.id;
}

void World::removeContinent(int id)
{
    for (quint16 &l : m_continentLabel)
        if (l == id)
            l = 0;
    continents.erase(std::remove_if(continents.begin(), continents.end(),
                                    [id](const Continent &c) { return c.id == id; }),
                     continents.end());
    for (Country &c : countries)
        if (c.continentId == id)
            c.continentId = 0;
}

void World::removeCountry(int id)
{
    for (quint16 &l : m_countryLabel)
        if (l == id)
            l = 0;
    countries.erase(std::remove_if(countries.begin(), countries.end(),
                                   [id](const Country &c) { return c.id == id; }),
                    countries.end());
}

static QHash<int, QPointF> labelPositions(const QVector<quint16> &labels)
{
    struct Acc { double sx = 0, sy = 0; qint64 n = 0; };
    QHash<int, Acc> acc;
    for (int i = 0; i < labels.size(); ++i) {
        if (const int id = labels[i]) {
            Acc &a = acc[id];
            a.sx += i % World::Width;
            a.sy += i / World::Width;
            ++a.n;
        }
    }

    QHash<int, QPointF> out;
    QHash<int, double> bestDist;
    QList<int> needSearch;
    for (auto it = acc.cbegin(); it != acc.cend(); ++it) {
        const QPointF c(it->sx / it->n, it->sy / it->n);
        out[it.key()] = c;
        const int cx = qBound(0, qRound(c.x()), World::Width - 1);
        const int cy = qBound(0, qRound(c.y()), World::Height - 1);
        if (labels[cy * World::Width + cx] != it.key()) {
            needSearch << it.key();
            bestDist[it.key()] = std::numeric_limits<double>::max();
        }
    }
    // For oddly shaped areas (like a "C") the centre is outside: use the nearest pixel inside.
    if (!needSearch.isEmpty()) {
        for (int i = 0; i < labels.size(); i += 3) {
            const int id = labels[i];
            if (!id || !bestDist.contains(id))
                continue;
            const QPointF p(i % World::Width, i / World::Width);
            const QPointF d = p - out[id];
            const double dist = d.x() * d.x() + d.y() * d.y();
            if (dist < bestDist[id]) {
                bestDist[id] = dist;
                out[id] = p;
            }
        }
    }
    return out;
}

QHash<int, QPointF> World::continentLabelPositions() const
{
    return labelPositions(m_continentLabel);
}

QHash<int, QPointF> World::countryLabelPositions() const
{
    return labelPositions(m_countryLabel);
}

QHash<int, int> World::countryAreas() const
{
    QHash<int, int> out;
    for (quint16 l : m_countryLabel)
        if (l)
            ++out[l];
    return out;
}

// ---- Rendering --------------------------------------------------------------

QImage World::render(ColorMode mode, int selectedContinent, int selectedCountry) const
{
    QVector<QRgb> continentRgb(m_nextContinentId + 1, kLandColor.rgb());
    QVector<QRgb> continentPale(m_nextContinentId + 1, kLandColor.rgb());
    for (const Continent &c : continents) {
        QColor col = c.color;
        if (c.id == selectedContinent && mode == ByContinent)
            col = col.lighter(125);
        continentRgb[c.id] = col.rgb();
        // Pale version shown under countries that are not drawn yet.
        QColor pale = c.color;
        pale.setHsv(pale.hsvHue(), pale.hsvSaturation() / 3, 235);
        continentPale[c.id] = pale.rgb();
    }
    QVector<QRgb> countryRgb(m_nextCountryId + 1, 0);
    for (const Country &c : countries) {
        QColor col = c.color;
        if (c.id == selectedCountry)
            col = col.lighter(125);
        countryRgb[c.id] = col.rgb();
    }

    QImage img(Width, Height, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    const QRgb land = kLandColor.rgb();
    for (int y = 0; y < Height; ++y) {
        const uchar *mask = m_landMask.constScanLine(y);
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < Width; ++x) {
            if (!mask[x])
                continue;
            const int i = y * Width + x;
            QRgb c = land;
            if (mode == ByContinent) {
                c = continentRgb.value(m_continentLabel[i], land);
            } else if (mode == ByCountry) {
                const quint16 country = m_countryLabel[i];
                if (country && countryRgb.value(country))
                    c = countryRgb[country];
                else
                    c = continentPale.value(m_continentLabel[i], land);
            }
            line[x] = c;
        }
    }
    return img;
}

// ---- Files ------------------------------------------------------------------

QJsonObject World::toJson() const
{
    QJsonArray land;
    for (const QPolygonF &poly : m_land.toSubpathPolygons())
        land.append(polygonToJson(poly));

    QJsonArray borders;
    for (const BorderStroke &b : m_borders)
        borders.append(QJsonObject { { "level", b.level }, { "points", polygonToJson(b.points) } });

    QJsonArray conts;
    for (const Continent &c : continents)
        conts.append(QJsonObject { { "id", c.id }, { "name", c.name }, { "color", c.color.name() } });

    QJsonArray ctrs;
    for (const Country &c : countries)
        ctrs.append(QJsonObject {
            { "id", c.id },
            { "name", c.name },
            { "continent", c.continentId },
            { "color", c.color.name() },
            { "flag", c.flag.toJson() },
        });

    return QJsonObject {
        { "app", "vlaggenspel" },
        { "version", 1 },
        { "width", Width },
        { "height", Height },
        { "land", land },
        { "borders", borders },
        { "continents", conts },
        { "countries", ctrs },
        { "continentPixels", labelsToString(m_continentLabel) },
        { "countryPixels", labelsToString(m_countryLabel) },
    };
}

bool World::fromJson(const QJsonObject &o, World &out, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (o.value("app").toString() != "vlaggenspel")
        return fail(QObject::tr("This is not a Vlaggenspel world file."));
    if (o.value("width").toInt() != Width || o.value("height").toInt() != Height)
        return fail(QObject::tr("This world has a different map size."));

    World w;
    w.m_land = QPainterPath();
    w.m_land.setFillRule(Qt::WindingFill);
    // Sub-paths were saved from a simplified (non-overlapping) path, so odd-even reproduces holes.
    w.m_land.setFillRule(Qt::OddEvenFill);
    for (const auto &v : o.value("land").toArray()) {
        w.m_land.addPolygon(polygonFromJson(v.toArray()));
        w.m_land.closeSubpath();
    }
    w.m_land = w.m_land.simplified();

    for (const auto &v : o.value("borders").toArray()) {
        const QJsonObject b = v.toObject();
        w.m_borders.append({ qBound(1, b.value("level").toInt(1), 2), polygonFromJson(b.value("points").toArray()) });
    }
    int maxContinent = 0, maxCountry = 0;
    for (const auto &v : o.value("continents").toArray()) {
        const QJsonObject c = v.toObject();
        Continent cont { c.value("id").toInt(), c.value("name").toString(), QColor(c.value("color").toString()) };
        maxContinent = qMax(maxContinent, cont.id);
        w.continents.append(cont);
    }
    for (const auto &v : o.value("countries").toArray()) {
        const QJsonObject c = v.toObject();
        Country ctr;
        ctr.id = c.value("id").toInt();
        ctr.name = c.value("name").toString();
        ctr.continentId = c.value("continent").toInt();
        ctr.color = QColor(c.value("color").toString());
        ctr.flag = Flag::fromJson(c.value("flag").toObject());
        maxCountry = qMax(maxCountry, ctr.id);
        w.countries.append(ctr);
    }
    if (!labelsFromString(o.value("continentPixels").toString(), w.m_continentLabel)
        || !labelsFromString(o.value("countryPixels").toString(), w.m_countryLabel))
        return fail(QObject::tr("The map data in this file is damaged."));

    for (quint16 l : w.m_continentLabel)
        maxContinent = qMax<int>(maxContinent, l);
    for (quint16 l : w.m_countryLabel)
        maxCountry = qMax<int>(maxCountry, l);
    w.m_nextContinentId = maxContinent + 1;
    w.m_nextCountryId = maxCountry + 1;

    w.rebuildLandMask();
    w.rebuildBorderMasks();
    out = w;
    return true;
}
