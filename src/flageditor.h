#pragma once

#include "flag.h"

#include <QDialog>

class QButtonGroup;
class QComboBox;
class QLineEdit;
class QListWidget;
class QRadioButton;
class QSlider;
class QSpinBox;
class QToolButton;

// Big clickable flag: click a stripe or shape to colour it, drag a shape to move it.
class FlagCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit FlagCanvas(QWidget *parent = nullptr);
    void setFlag(const Flag &f);
    const Flag &flag() const { return m_flag; }
    void setBrushColor(const QColor &c) { m_brush = c; }
    void setSelectedEmblem(int i);

signals:
    void flagChanged();
    void emblemSelected(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    QSize sizeHint() const override { return QSize(480, 320); }

private:
    QRectF flagRect() const;

    Flag m_flag;
    QColor m_brush = Qt::red;
    int m_selected = -1;
    int m_pressEmblem = -1;
    QPointF m_pressPos;
    bool m_dragging = false;
};

class FlagEditor : public QDialog
{
    Q_OBJECT
public:
    FlagEditor(const QString &countryName, const Flag &flag, QWidget *parent = nullptr);

    Flag flag() const;
    QString countryName() const;

private:
    void setBrushColor(const QColor &c);
    void refreshControls();
    void refreshEmblemList();
    void onStripeCountChanged(int n);
    void onEmblemSliders();
    void addEmblem();
    void removeEmblem();
    int currentEmblem() const;

    FlagCanvas *m_canvas;
    QLineEdit *m_name;
    QToolButton *m_currentColor;
    QRadioButton *m_horizontal;
    QRadioButton *m_vertical;
    QSpinBox *m_stripeCount;
    QListWidget *m_emblems;
    QComboBox *m_newShape;
    QSlider *m_x;
    QSlider *m_y;
    QSlider *m_size;
    QColor m_brush;
    bool m_updating = false;
};
