#include "mapview.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

namespace {
const QColor kSeaTop(86, 156, 204);
const QColor kSeaBottom(52, 112, 168);
const QColor kCoast(40, 70, 95);
const QColor kContinentBorder(70, 40, 30);
const QColor kCountryBorder(60, 60, 60);
} // namespace

MapView::MapView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);
    setCursor(Qt::CrossCursor);
}

void MapView::setWorld(World *world)
{
    m_world = world;
    worldChanged();
}

void MapView::setStage(Stage stage)
{
    m_stage = stage;
    switch (stage) {
    case LandStage: setTool(DrawLand); break;
    case ContinentStage:
    case CountryStage: setTool(DrawBorder); break;
    case FlagStage: setTool(Pick); break;
    }
    worldChanged();
}

void MapView::setTool(Tool tool)
{
    m_tool = tool;
    QString text;
    switch (tool) {
    case DrawLand:
        text = tr("Draw the outline of an island or continent with the left mouse button. "
                  "Release to close the shape. Right mouse button erases. Scroll to zoom, middle button to move.");
        break;
    case EraseLand:
        text = tr("Draw around the part of the land you want to turn into sea.");
        break;
    case DrawBorder:
        text = m_stage == ContinentStage
            ? tr("Draw a line across the land, from coast to coast, to split it into continents. Then use the bucket.")
            : tr("Draw a line across a continent, from coast or border to the other side, to split it into countries. Then use the bucket.");
        break;
    case EraseBorder:
        text = tr("Click on a border line to remove it.");
        break;
    case Fill:
        text = m_stage == ContinentStage
            ? tr("Click on a piece of land to make it a new continent, or to select it. Shift+click adds the piece to the selected continent.")
            : tr("Click inside a continent to make a new country, or to select it. Shift+click adds the piece to the selected country.");
        break;
    case Pick:
        text = tr("Click a country to choose it. Double-click to design its flag.");
        break;
    }
    emit hint(text);
    update();
}

void MapView::setSelectedContinent(int id)
{
    if (m_selectedContinent == id)
        return;
    m_selectedContinent = id;
    rebuildImage();
    update();
}

void MapView::setSelectedCountry(int id)
{
    if (m_selectedCountry == id)
        return;
    m_selectedCountry = id;
    rebuildImage();
    update();
}

void MapView::worldChanged()
{
    if (!m_world)
        return;
    if (!m_world->continent(m_selectedContinent))
        m_selectedContinent = 0;
    if (!m_world->country(m_selectedCountry))
        m_selectedCountry = 0;
    rebuildImage();
    update();
}

void MapView::rebuildImage()
{
    if (!m_world)
        return;
    World::ColorMode mode = World::LandOnly;
    if (m_stage == ContinentStage)
        mode = World::ByContinent;
    else if (m_stage == CountryStage || m_stage == FlagStage)
        mode = World::ByCountry;
    m_image = m_world->render(mode, m_selectedContinent, m_selectedCountry);

    m_labelPos = m_stage == ContinentStage ? m_world->continentLabelPositions()
               : m_stage == LandStage      ? QHash<int, QPointF>()
                                           : m_world->countryLabelPositions();
    m_flagCache.clear();
    if (m_stage == FlagStage)
        for (const Country &c : m_world->countries)
            m_flagCache.insert(c.id, c.flag.toPixmap(96));
}

QTransform MapView::mapTransform() const
{
    QTransform t;
    t.translate(m_offset.x(), m_offset.y());
    t.scale(m_scale, m_scale);
    return t;
}

QPointF MapView::toMap(const QPointF &widgetPos) const
{
    return (widgetPos - m_offset) / m_scale;
}

void MapView::zoomToFit()
{
    const double margin = 12;
    const double sx = (width() - 2 * margin) / World::Width;
    const double sy = (height() - 2 * margin) / World::Height;
    m_scale = qMax(0.05, qMin(sx, sy));
    m_offset = QPointF((width() - World::Width * m_scale) / 2, (height() - World::Height * m_scale) / 2);
    m_fitted = true;
    update();
}

void MapView::resizeEvent(QResizeEvent *)
{
    if (m_fitted)
        zoomToFit();
}

void MapView::paintWorld(QPainter &p, bool forExport) const
{
    const QRectF mapRect(0, 0, World::Width, World::Height);

    // Sea
    QLinearGradient sea(0, 0, 0, World::Height);
    sea.setColorAt(0, kSeaTop);
    sea.setColorAt(1, kSeaBottom);
    p.fillRect(mapRect, sea);
    p.setPen(QPen(QColor(255, 255, 255, 28), 1.5));
    for (int x = 100; x < World::Width; x += 100)
        p.drawLine(QPointF(x, 0), QPointF(x, World::Height));
    for (int y = 100; y < World::Height; y += 100)
        p.drawLine(QPointF(0, y), QPointF(World::Width, y));

    if (!m_world)
        return;

    // Land colours
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 2.0 || forExport);
    p.drawImage(mapRect, m_image);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Shallow-water glow and coastline
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(200, 235, 255, 70), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(m_world->land());
    p.setPen(QPen(kCoast, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(m_world->land());

    // Borders, clipped to land so lines that overshoot into the sea are hidden.
    if (m_stage != LandStage) {
        p.save();
        p.setClipPath(m_world->land());
        for (const BorderStroke &b : m_world->borders()) {
            if (b.level == 2 && m_stage == ContinentStage)
                continue;
            const bool cont = b.level == 1;
            p.setPen(QPen(cont ? kContinentBorder : kCountryBorder, cont ? 4 : 3,
                          Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(b.points);
            if (!cont) {
                p.setPen(QPen(QColor(255, 255, 255, 120), 1, Qt::DashLine, Qt::RoundCap));
                p.drawPolyline(b.points);
            }
        }
        p.restore();
        // Show overshooting parts faintly while editing so they can still be clicked away.
        if (!forExport && (m_tool == EraseBorder || m_tool == DrawBorder)) {
            p.setPen(QPen(QColor(0, 0, 0, 50), 2));
            const int level = m_stage == ContinentStage ? 1 : 2;
            for (const BorderStroke &b : m_world->borders())
                if (b.level == level)
                    p.drawPolyline(b.points);
        }
    }

    // Names (and flags) on top
    QFont font = p.font();
    font.setBold(true);
    const double fontPx = m_stage == ContinentStage ? 18 : 13;
    font.setPixelSize(qMax(1, qRound(fontPx * (forExport ? 1.4 : 1.0))));
    const QTransform saved = p.transform();
    for (auto it = m_labelPos.cbegin(); it != m_labelPos.cend(); ++it) {
        QString name;
        if (m_stage == ContinentStage) {
            if (const Continent *c = m_world->continent(it.key()))
                name = c->name.toUpper();
        } else if (const Country *c = m_world->country(it.key())) {
            name = c->name;
        }
        if (name.isEmpty())
            continue;

        // Draw text at a fixed screen size: map the anchor, then reset the transform.
        const QPointF anchor = saved.map(it.value());
        p.setTransform(forExport ? saved : QTransform());
        p.setFont(font);
        QPointF textPos = anchor;
        if (m_stage == FlagStage && m_flagCache.contains(it.key())) {
            const QPixmap &pm = m_flagCache[it.key()];
            const double fw = forExport ? 54 : 36;
            const QRectF fr(anchor.x() - fw / 2, anchor.y() - fw / kFlagAspect - 2, fw, fw / kFlagAspect);
            p.drawPixmap(fr, pm, pm.rect());
            textPos.ry() += QFontMetricsF(font).ascent();
        }
        const QFontMetricsF fm(font);
        const QRectF tr(textPos.x() - fm.horizontalAdvance(name) / 2, textPos.y() - fm.ascent() / 2,
                        fm.horizontalAdvance(name), fm.height());
        QPainterPath text;
        text.addText(tr.left(), tr.top() + fm.ascent(), font, name);
        p.setPen(QPen(QColor(255, 255, 255, 220), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(text);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 30, 30));
        p.drawPath(text);
        p.setTransform(saved);
    }
}

void MapView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(45, 48, 56));
    p.setTransform(mapTransform());
    paintWorld(p, false);

    // Line or shape being drawn right now
    if (m_drawing && m_stroke.size() > 1) {
        p.setRenderHint(QPainter::Antialiasing, true);
        const double w = 2.0 / m_scale;
        if (m_tool == DrawLand || m_tool == EraseLand) {
            p.setBrush(m_eraseStroke ? QColor(60, 120, 200, 120) : QColor(232, 220, 181, 150));
            p.setPen(QPen(m_eraseStroke ? QColor(20, 50, 120) : kCoast, w, Qt::DashLine));
            p.drawPolygon(m_stroke);
        } else {
            p.setPen(QPen(m_stage == ContinentStage ? kContinentBorder : kCountryBorder,
                          m_stage == ContinentStage ? 4 : 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(m_stroke);
        }
    }
}

void MapView::mousePressEvent(QMouseEvent *e)
{
    if (!m_world)
        return;
    const QPointF mp = toMap(e->position());

    const bool panButton = e->button() == Qt::MiddleButton
        || (e->button() == Qt::RightButton && m_stage != LandStage);
    if (panButton) {
        m_panning = true;
        m_panStart = e->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    switch (m_tool) {
    case DrawLand:
    case EraseLand:
    case DrawBorder:
        if (e->button() == Qt::LeftButton || e->button() == Qt::RightButton) {
            m_drawing = true;
            m_eraseStroke = m_tool == EraseLand || e->button() == Qt::RightButton;
            m_stroke = QPolygonF() << mp;
        }
        break;
    case EraseBorder:
        if (e->button() == Qt::LeftButton) {
            World copy = *m_world;
            if (copy.removeBorderNear(m_stage == ContinentStage ? 1 : 2, mp, 12.0 / m_scale + 4)) {
                emit aboutToEdit();
                *m_world = copy;
                worldChanged();
                emit edited();
            }
        }
        break;
    case Fill:
        if (e->button() == Qt::LeftButton)
            fillAt(mp, e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier));
        break;
    case Pick:
        if (e->button() == Qt::LeftButton) {
            const int id = m_world->countryAt(int(mp.x()), int(mp.y()));
            setSelectedCountry(id);
            emit countrySelected(id);
        }
        break;
    }
}

void MapView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_panning) {
        m_offset += e->position() - m_panStart;
        m_panStart = e->position();
        m_fitted = false;
        update();
        return;
    }
    if (m_drawing) {
        const QPointF mp = toMap(e->position());
        const QPointF d = mp - m_stroke.last();
        if (d.x() * d.x() + d.y() * d.y() > 2.0)
            m_stroke << mp;
        update();
    }
}

void MapView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning) {
        m_panning = false;
        setCursor(Qt::CrossCursor);
        return;
    }
    if (m_drawing) {
        m_stroke << toMap(e->position());
        m_drawing = false;
        finishStroke(m_eraseStroke);
        m_stroke.clear();
        update();
    }
}

void MapView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_tool == Pick && e->button() == Qt::LeftButton && m_world) {
        const QPointF mp = toMap(e->position());
        if (const int id = m_world->countryAt(int(mp.x()), int(mp.y())))
            emit countryActivated(id);
        return;
    }
    mousePressEvent(e);
}

void MapView::wheelEvent(QWheelEvent *e)
{
    const double steps = e->angleDelta().y() / 120.0;
    const double factor = std::pow(1.2, steps);
    const QPointF anchor = toMap(e->position());
    m_scale = qBound(0.05, m_scale * factor, 30.0);
    m_offset = e->position() - anchor * m_scale;
    m_fitted = false;
    update();
}

void MapView::finishStroke(bool erase)
{
    if (m_stroke.size() < 3)
        return;

    if (m_tool == DrawLand || m_tool == EraseLand) {
        // Ignore tiny accidental scribbles.
        if (m_stroke.boundingRect().width() < 4 && m_stroke.boundingRect().height() < 4)
            return;
        emit aboutToEdit();
        if (erase)
            m_world->eraseLand(m_stroke);
        else
            m_world->addLand(m_stroke);
    } else if (m_tool == DrawBorder) {
        if (erase)
            return;
        emit aboutToEdit();
        m_world->addBorder(m_stage == ContinentStage ? 1 : 2, m_stroke);
    }
    worldChanged();
    emit edited();
}

void MapView::fillAt(const QPointF &mp, bool addToSelection)
{
    const int x = int(mp.x()), y = int(mp.y());
    if (!m_world->isLand(x, y))
        return;

    if (m_stage == ContinentStage) {
        const int existing = m_world->continentAt(x, y);
        if (addToSelection && m_selectedContinent) {
            if (existing == m_selectedContinent)
                return;
            World copy = *m_world;
            if (!copy.fillContinent(x, y, m_selectedContinent))
                return;
            emit aboutToEdit();
            *m_world = copy;
        } else if (existing) {
            setSelectedContinent(existing);
            emit continentSelected(existing);
            return;
        } else {
            World copy = *m_world;
            const int id = copy.newContinent();
            if (!copy.fillContinent(x, y, id))
                return; // clicked exactly on a border line
            emit aboutToEdit();
            *m_world = copy;
            m_selectedContinent = id;
            emit continentSelected(id);
        }
    } else if (m_stage == CountryStage) {
        const int existing = m_world->countryAt(x, y);
        if (addToSelection && m_selectedCountry) {
            if (existing == m_selectedCountry)
                return;
            World copy = *m_world;
            if (!copy.fillCountry(x, y, m_selectedCountry))
                return;
            emit aboutToEdit();
            *m_world = copy;
        } else if (existing) {
            setSelectedCountry(existing);
            emit countrySelected(existing);
            return;
        } else {
            World copy = *m_world;
            const int id = copy.newCountry(m_world->continentAt(x, y));
            if (!copy.fillCountry(x, y, id))
                return;
            emit aboutToEdit();
            *m_world = copy;
            m_selectedCountry = id;
            emit countrySelected(id);
        }
    }
    worldChanged();
    emit edited();
}

QImage MapView::exportImage() const
{
    QImage img(World::Width, World::Height, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    paintWorld(p, true);
    return img;
}
