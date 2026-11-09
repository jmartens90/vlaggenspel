#pragma once

#include "world.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPolygonF>
#include <QWidget>

// Shows the world and turns mouse input into drawing / dividing / selecting.
class MapView : public QWidget
{
    Q_OBJECT

public:
    enum Stage { LandStage, ContinentStage, CountryStage, FlagStage };
    enum Tool { DrawLand, EraseLand, DrawBorder, EraseBorder, Fill, Pick };

    explicit MapView(QWidget *parent = nullptr);

    void setWorld(World *world);
    void setStage(Stage stage);
    Stage stage() const { return m_stage; }
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    void setSelectedContinent(int id);
    void setSelectedCountry(int id);
    int selectedContinent() const { return m_selectedContinent; }
    int selectedCountry() const { return m_selectedCountry; }

    // Call after the world changed from outside (undo, loading, renaming...).
    void worldChanged();
    void zoomToFit();
    // When on, a bucket tap adds the area to the selected continent/country (like Shift+click).
    void setAddMode(bool on) { m_addMode = on; }

    QImage exportImage() const;

signals:
    // Emitted just before the view changes the world, so the window can save an undo step.
    void aboutToEdit();
    void edited();
    void continentSelected(int id);
    void countrySelected(int id);
    void countryActivated(int id); // double click in flag stage
    void hint(const QString &text);

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *) override;

private:
    QPointF toMap(const QPointF &widgetPos) const;
    QTransform mapTransform() const;
    void rebuildImage();
    void paintWorld(QPainter &p, bool forExport) const;
    void pointerDown(const QPointF &pos, bool alternate, bool addToSelection);
    void pointerMove(const QPointF &pos);
    void pointerUp(const QPointF &pos);
    void cancelStroke();
    void zoomAt(const QPointF &pos, double factor);
    void finishStroke(bool erase);
    void fillAt(const QPointF &mapPos, bool addToSelection);

    World *m_world = nullptr;
    Stage m_stage = LandStage;
    Tool m_tool = DrawLand;
    int m_selectedContinent = 0;
    int m_selectedCountry = 0;

    QImage m_image;
    QHash<int, QPointF> m_labelPos;
    QHash<int, QPixmap> m_flagCache;

    double m_scale = 1.0;
    QPointF m_offset;
    bool m_fitted = true; // follow window size until the user zooms

    bool m_drawing = false;
    bool m_eraseStroke = false;
    QPolygonF m_stroke;
    bool m_panning = false;
    QPointF m_panStart;
    bool m_addMode = false;

    bool m_pinching = false;
    double m_pinchStartScale = 1.0;
    double m_pinchStartDist = 0.0;
    QPointF m_pinchAnchor;
    QElapsedTimer m_lastTap;
    QPointF m_lastTapPos;
};
