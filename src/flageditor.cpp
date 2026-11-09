#include "flageditor.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

// ---- FlagCanvas ---------------------------------------------------------------

FlagCanvas::FlagCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(300, 200);
    setCursor(Qt::PointingHandCursor);
}

void FlagCanvas::setFlag(const Flag &f)
{
    m_flag = f;
    if (m_selected >= m_flag.emblems.size())
        m_selected = -1;
    update();
}

void FlagCanvas::setSelectedEmblem(int i)
{
    m_selected = i;
    update();
}

QRectF FlagCanvas::flagRect() const
{
    const QRectF area = QRectF(rect()).adjusted(16, 16, -16, -16);
    double w = area.width();
    double h = w / kFlagAspect;
    if (h > area.height()) {
        h = area.height();
        w = h * kFlagAspect;
    }
    return QRectF(area.center().x() - w / 2, area.center().y() - h / 2, w, h);
}

void FlagCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = flagRect();
    // Soft shadow so the flag looks like a little card.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawRoundedRect(r.translated(4, 5), 4, 4);
    m_flag.paint(p, r);
    if (m_selected >= 0 && m_selected < m_flag.emblems.size()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255), 3, Qt::DashLine));
        p.drawPath(m_flag.emblems[m_selected].path(r));
        p.setPen(QPen(QColor(0, 0, 0), 1, Qt::DashLine));
        p.drawPath(m_flag.emblems[m_selected].path(r));
    }
}

void FlagCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_pressPos = e->position();
    m_dragging = false;
    m_pressEmblem = m_flag.emblemAt(flagRect(), e->position());
    if (m_pressEmblem >= 0) {
        m_selected = m_pressEmblem;
        emit emblemSelected(m_selected);
        update();
    }
}

void FlagCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton) || m_pressEmblem < 0)
        return;
    if (!m_dragging && (e->position() - m_pressPos).manhattanLength() < 4)
        return;
    m_dragging = true;
    const QRectF r = flagRect();
    const QPointF d = e->position() - m_pressPos;
    m_pressPos = e->position();
    Emblem &em = m_flag.emblems[m_pressEmblem];
    em.x = qBound(0.0, em.x + d.x() / r.width(), 1.0);
    em.y = qBound(0.0, em.y + d.y() / r.height(), 1.0);
    update();
    emit flagChanged();
}

void FlagCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_dragging) {
        m_dragging = false;
        return;
    }
    // A click (no drag) colours whatever was clicked.
    const QRectF r = flagRect();
    if (m_pressEmblem >= 0) {
        m_flag.emblems[m_pressEmblem].color = m_brush;
    } else {
        const int s = m_flag.stripeAt(r, e->position());
        if (s < 0)
            return;
        m_flag.stripes[s] = m_brush;
    }
    update();
    emit flagChanged();
}

// ---- FlagEditor ---------------------------------------------------------------

static QIcon colorIcon(const QColor &c, int size = 22)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 120), 1));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(1, 1, size - 2, size - 2), 4, 4);
    return QIcon(pm);
}

static QIcon emblemIcon(const Emblem &e)
{
    Flag f;
    f.stripes = { QColor(220, 220, 220) };
    Emblem copy = e;
    f.emblems = { copy };
    return QIcon(f.toPixmap(36));
}

FlagEditor::FlagEditor(const QString &countryName, const Flag &flag, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Design a flag"));

    m_canvas = new FlagCanvas;
    m_canvas->setFlag(flag);

    // Colour palette
    auto *paletteRow = new QHBoxLayout;
    paletteRow->setSpacing(4);
    paletteRow->addWidget(new QLabel(tr("Paint:")));
    m_currentColor = new QToolButton;
    m_currentColor->setIconSize(QSize(34, 34));
    m_currentColor->setToolTip(tr("The colour you paint with"));
    paletteRow->addWidget(m_currentColor);
    paletteRow->addSpacing(8);
    // Two rows of colours so the designer also fits on a phone held upright.
    auto *grid = new QGridLayout;
    grid->setSpacing(2);
    int n = 0;
    for (const QColor &c : Flag::palette()) {
        auto *b = new QToolButton;
        b->setIcon(colorIcon(c));
        b->setIconSize(QSize(24, 24));
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [this, c] { setBrushColor(c); });
        grid->addWidget(b, n / 6, n % 6);
        ++n;
    }
    auto *more = new QToolButton;
    more->setText(tr("More…"));
    connect(more, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_brush, this, tr("Pick a colour"));
        if (c.isValid())
            setBrushColor(c);
    });
    paletteRow->addLayout(grid);
    paletteRow->addWidget(more);
    paletteRow->addStretch();

    auto *tip = new QLabel(tr("Pick a colour, then click a stripe or a shape on the flag to paint it. "
                              "Drag a shape to move it."));
    tip->setWordWrap(true);
    tip->setStyleSheet("color: palette(mid);");

    auto *left = new QVBoxLayout;
    left->addWidget(m_canvas, 1);
    left->addLayout(paletteRow);
    left->addWidget(tip);

    // Right side controls
    m_name = new QLineEdit(countryName);
    m_name->setPlaceholderText(tr("Name of the country"));

    auto *stripesBox = new QGroupBox(tr("Stripes"));
    m_horizontal = new QRadioButton(tr("Lying down ═"));
    m_vertical = new QRadioButton(tr("Standing up ║"));
    m_stripeCount = new QSpinBox;
    m_stripeCount->setRange(1, 9);
    auto *stripesForm = new QFormLayout(stripesBox);
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(m_horizontal);
    dirRow->addWidget(m_vertical);
    stripesForm->addRow(dirRow);
    stripesForm->addRow(tr("How many:"), m_stripeCount);

    auto *shapesBox = new QGroupBox(tr("Shapes"));
    m_emblems = new QListWidget;
    m_emblems->setIconSize(QSize(36, 24));
    m_emblems->setMaximumHeight(130);
    m_newShape = new QComboBox;
    for (int s = 0; s < Emblem::ShapeCount; ++s) {
        Emblem e;
        e.shape = Emblem::Shape(s);
        e.color = QColor(200, 30, 40);
        m_newShape->addItem(emblemIcon(e), Emblem::shapeName(e.shape), s);
    }
    auto *add = new QPushButton(tr("Add"));
    auto *remove = new QPushButton(tr("Remove"));
    m_x = new QSlider(Qt::Horizontal);
    m_y = new QSlider(Qt::Horizontal);
    m_size = new QSlider(Qt::Horizontal);
    m_x->setRange(0, 100);
    m_y->setRange(0, 100);
    m_size->setRange(5, 100);
    auto *addRow = new QHBoxLayout;
    addRow->addWidget(m_newShape, 1);
    addRow->addWidget(add);
    addRow->addWidget(remove);
    auto *shapesForm = new QFormLayout(shapesBox);
    shapesForm->addRow(addRow);
    shapesForm->addRow(m_emblems);
    shapesForm->addRow(tr("Size:"), m_size);
    shapesForm->addRow(tr("Left ↔ right:"), m_x);
    shapesForm->addRow(tr("Up ↕ down:"), m_y);

    auto *surprise = new QPushButton(tr("🎲 Surprise me"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto *right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("Country name:")));
    right->addWidget(m_name);
    right->addWidget(stripesBox);
    right->addWidget(shapesBox);
    right->addWidget(surprise);
    right->addStretch();
    right->addWidget(buttons);

    m_mainLayout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_mainLayout->addLayout(left, 3);
    m_mainLayout->addLayout(right, 2);
    updateDirection();

    // Wiring
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_canvas, &FlagCanvas::flagChanged, this, [this] { refreshControls(); });
    connect(m_canvas, &FlagCanvas::emblemSelected, this, [this](int i) {
        m_updating = true;
        m_emblems->setCurrentRow(i);
        m_updating = false;
        refreshControls();
    });
    auto setLayout = [this](Flag::Layout l) {
        if (m_updating)
            return;
        Flag f = m_canvas->flag();
        f.layout = l;
        m_canvas->setFlag(f);
    };
    connect(m_horizontal, &QRadioButton::toggled, this, [=](bool on) { if (on) setLayout(Flag::Horizontal); });
    connect(m_vertical, &QRadioButton::toggled, this, [=](bool on) { if (on) setLayout(Flag::Vertical); });
    connect(m_stripeCount, &QSpinBox::valueChanged, this, &FlagEditor::onStripeCountChanged);
    connect(add, &QPushButton::clicked, this, &FlagEditor::addEmblem);
    connect(remove, &QPushButton::clicked, this, &FlagEditor::removeEmblem);
    connect(m_emblems, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating)
            return;
        m_canvas->setSelectedEmblem(row);
        refreshControls();
    });
    for (QSlider *s : { m_x, m_y, m_size })
        connect(s, &QSlider::valueChanged, this, &FlagEditor::onEmblemSliders);
    connect(surprise, &QPushButton::clicked, this, [this] {
        m_canvas->setFlag(Flag::random());
        refreshEmblemList();
        refreshControls();
    });

    setBrushColor(Flag::palette().first());
    refreshEmblemList();
    refreshControls();
    resize(900, 520);
}

void FlagEditor::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    updateDirection();
}

// On a phone held upright, put the controls under the flag instead of next to it.
void FlagEditor::updateDirection()
{
    const QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect r = scr ? scr->availableGeometry() : QRect(0, 0, 1024, 768);
    m_mainLayout->setDirection(r.height() > r.width() ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}

Flag FlagEditor::flag() const
{
    return m_canvas->flag();
}

QString FlagEditor::countryName() const
{
    return m_name->text().trimmed();
}

void FlagEditor::setBrushColor(const QColor &c)
{
    m_brush = c;
    m_canvas->setBrushColor(c);
    m_currentColor->setIcon(colorIcon(c, 34));
}

int FlagEditor::currentEmblem() const
{
    const int row = m_emblems->currentRow();
    return row >= 0 && row < m_canvas->flag().emblems.size() ? row : -1;
}

void FlagEditor::refreshEmblemList()
{
    m_updating = true;
    const int row = m_emblems->currentRow();
    m_emblems->clear();
    const Flag &f = m_canvas->flag();
    for (const Emblem &e : f.emblems)
        m_emblems->addItem(new QListWidgetItem(emblemIcon(e), Emblem::shapeName(e.shape)));
    if (!f.emblems.isEmpty())
        m_emblems->setCurrentRow(qBound(0, row, int(f.emblems.size()) - 1));
    m_canvas->setSelectedEmblem(m_emblems->currentRow());
    m_updating = false;
}

void FlagEditor::refreshControls()
{
    m_updating = true;
    const Flag &f = m_canvas->flag();
    m_horizontal->setChecked(f.layout == Flag::Horizontal);
    m_vertical->setChecked(f.layout == Flag::Vertical);
    m_stripeCount->setValue(int(f.stripes.size()));

    // Keep list icons in sync with colours painted on the canvas.
    for (int i = 0; i < f.emblems.size() && i < m_emblems->count(); ++i)
        m_emblems->item(i)->setIcon(emblemIcon(f.emblems[i]));

    const int i = currentEmblem();
    for (QSlider *s : { m_x, m_y, m_size })
        s->setEnabled(i >= 0);
    if (i >= 0) {
        const Emblem &e = f.emblems[i];
        m_x->setValue(qRound(e.x * 100));
        m_y->setValue(qRound(e.y * 100));
        m_size->setValue(qRound(e.size * 100));
        // Some shapes ignore the position; grey those sliders out.
        const bool fixedPos = e.shape == Emblem::Saltire || e.shape == Emblem::HoistTriangle
            || e.shape == Emblem::Canton;
        m_x->setEnabled(!fixedPos);
        m_y->setEnabled(!fixedPos);
    }
    m_updating = false;
}

void FlagEditor::onStripeCountChanged(int n)
{
    if (m_updating)
        return;
    Flag f = m_canvas->flag();
    const auto &pal = Flag::palette();
    while (f.stripes.size() < n) {
        // New stripes alternate between the first two colours so they are visible straight away.
        const QColor next = f.stripes.size() >= 2 ? f.stripes[f.stripes.size() - 2]
                                                  : pal[(pal.indexOf(f.stripes.last()) + 5) % pal.size()];
        f.stripes << next;
    }
    while (f.stripes.size() > n)
        f.stripes.removeLast();
    m_canvas->setFlag(f);
}

void FlagEditor::onEmblemSliders()
{
    if (m_updating)
        return;
    const int i = currentEmblem();
    if (i < 0)
        return;
    Flag f = m_canvas->flag();
    f.emblems[i].x = m_x->value() / 100.0;
    f.emblems[i].y = m_y->value() / 100.0;
    f.emblems[i].size = m_size->value() / 100.0;
    m_canvas->setFlag(f);
}

void FlagEditor::addEmblem()
{
    Flag f = m_canvas->flag();
    Emblem e;
    e.shape = Emblem::Shape(m_newShape->currentData().toInt());
    e.color = m_brush;
    switch (e.shape) {
    case Emblem::Cross: e.x = 0.36; e.size = 0.3; break;
    case Emblem::Saltire: e.size = 0.25; break;
    case Emblem::HoistTriangle: e.size = 0.4; break;
    case Emblem::Canton: e.size = 0.45; break;
    case Emblem::Star: e.size = 0.5; break;
    default: e.size = 0.4; break;
    }
    f.emblems << e;
    m_canvas->setFlag(f);
    refreshEmblemList();
    m_updating = true;
    m_emblems->setCurrentRow(int(f.emblems.size()) - 1);
    m_updating = false;
    m_canvas->setSelectedEmblem(int(f.emblems.size()) - 1);
    refreshControls();
}

void FlagEditor::removeEmblem()
{
    const int i = currentEmblem();
    if (i < 0)
        return;
    Flag f = m_canvas->flag();
    f.emblems.removeAt(i);
    m_canvas->setFlag(f);
    refreshEmblemList();
    refreshControls();
}
