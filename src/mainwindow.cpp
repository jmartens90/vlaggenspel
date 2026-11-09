#include "mainwindow.h"
#include "flageditor.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kMaxUndo = 40;

QIcon swatch(const QColor &c, int w = 20, int h = 20)
{
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 110), 1));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), 4, 4);
    return QIcon(pm);
}

// Small drawn icons so the app needs no image files.
QIcon toolIcon(const QString &kind)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (kind == "land" || kind == "sea") {
        QPainterPath blob;
        blob.moveTo(6, 18);
        blob.cubicTo(4, 6, 18, 2, 24, 9);
        blob.cubicTo(31, 15, 27, 27, 17, 26);
        blob.cubicTo(10, 28, 7, 24, 6, 18);
        p.setBrush(kind == "land" ? QColor(232, 220, 181) : QColor(86, 156, 204));
        p.setPen(QPen(kind == "land" ? QColor(40, 70, 95) : QColor(20, 50, 120), 2, Qt::DashLine));
        p.drawPath(blob);
    } else if (kind == "border" || kind == "noborder") {
        p.setPen(QPen(QColor(70, 40, 30), 3, Qt::SolidLine, Qt::RoundCap));
        QPainterPath line;
        line.moveTo(5, 27);
        line.cubicTo(12, 18, 10, 12, 26, 5);
        p.drawPath(line);
        if (kind == "noborder") {
            p.setPen(QPen(QColor(210, 30, 30), 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(8, 8, 24, 24);
            p.drawLine(24, 8, 8, 24);
        }
    } else if (kind == "fill") {
        p.setPen(QPen(QColor(50, 50, 50), 2));
        p.setBrush(QColor(255, 210, 63));
        QPolygonF bucket;
        bucket << QPointF(7, 12) << QPointF(21, 12) << QPointF(19, 27) << QPointF(9, 27);
        p.drawPolygon(bucket);
        p.setBrush(QColor(46, 134, 171));
        p.drawEllipse(QPointF(25, 22), 3, 4);
    } else if (kind == "pick") {
        p.setPen(QPen(QColor(50, 50, 50), 2));
        p.drawLine(9, 4, 9, 29);
        p.setBrush(QColor(215, 38, 61));
        p.drawRect(10, 5, 16, 11);
    }
    return QIcon(pm);
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_view = new MapView;
    setCentralWidget(m_view);

    buildMenus();
    buildToolBar();
    buildStageBar();
    buildSidePanel();

    m_hint = new QLabel;
    m_hint->setWordWrap(true);
    statusBar()->addWidget(m_hint, 1);

    connect(m_view, &MapView::hint, m_hint, &QLabel::setText);
    connect(m_view, &MapView::aboutToEdit, this, &MainWindow::saveUndo);
    connect(m_view, &MapView::edited, this, &MainWindow::markEdited);
    connect(m_view, &MapView::continentSelected, this, [this](int) { refreshSidePanel(); });
    connect(m_view, &MapView::countrySelected, this, [this](int) { refreshSidePanel(); });
    connect(m_view, &MapView::countryActivated, this, &MainWindow::designFlag);

    m_view->setWorld(&m_world);
    setStage(MapView::LandStage);
    refreshTitle();
    resize(1400, 900);
}

// ---- Building the window ------------------------------------------------------

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New world"), QKeySequence::New, this, &MainWindow::newWorld);
    file->addAction(tr("&Open…"), QKeySequence::Open, this, &MainWindow::openWorld);
    file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::saveWorld);
    file->addAction(tr("Save &as…"), QKeySequence::SaveAs, this, &MainWindow::saveWorldAs);
    file->addSeparator();
    file->addAction(tr("&Export map as picture…"), this, &MainWindow::exportPng);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    m_undoAct = edit->addAction(tr("&Undo"), QKeySequence::Undo, this, &MainWindow::undo);
    m_redoAct = edit->addAction(tr("&Redo"), QKeySequence::Redo, this, &MainWindow::redo);
    m_undoAct->setEnabled(false);
    m_redoAct->setEnabled(false);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("Show &whole map"), QKeySequence(Qt::Key_Home), m_view, &MapView::zoomToFit);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&How to play"), QKeySequence::HelpContents, this, &MainWindow::showHelp);
}

void MainWindow::buildStageBar()
{
    auto *bar = new QToolBar(tr("Steps"));
    bar->setMovable(false);
    bar->setObjectName("steps");
    m_stageButtons = new QButtonGroup(this);
    const QStringList names { tr("1  Draw the land"), tr("2  Make continents"),
                              tr("3  Make countries"), tr("4  Names && flags") };
    for (int i = 0; i < names.size(); ++i) {
        auto *b = new QPushButton(names[i]);
        b->setCheckable(true);
        b->setMinimumHeight(34);
        b->setStyleSheet("QPushButton { padding: 4px 14px; font-weight: bold; }"
                         "QPushButton:checked { background: #2e86ab; color: white; border-radius: 4px; }");
        m_stageButtons->addButton(b, i);
        bar->addWidget(b);
        if (i + 1 < names.size()) {
            auto *arrow = new QLabel("  ›  ");
            bar->addWidget(arrow);
        }
    }
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    m_nextButton = new QPushButton(tr("Next step  →"));
    m_nextButton->setMinimumHeight(34);
    bar->addWidget(m_nextButton);
    connect(m_nextButton, &QPushButton::clicked, this, [this] {
        setStage(MapView::Stage(qMin(int(MapView::FlagStage), int(m_view->stage()) + 1)));
    });
    connect(m_stageButtons, &QButtonGroup::idClicked, this, [this](int id) { setStage(MapView::Stage(id)); });
    addToolBar(Qt::TopToolBarArea, bar);
    addToolBarBreak(Qt::TopToolBarArea);
    // Put the steps above the tools.
    removeToolBar(m_tools);
    addToolBar(Qt::TopToolBarArea, m_tools);
    m_tools->show();
}

void MainWindow::buildToolBar()
{
    m_tools = new QToolBar(tr("Tools"));
    m_tools->setObjectName("tools");
    m_tools->setMovable(false);
    m_tools->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_tools->setIconSize(QSize(24, 24));
    m_toolGroup = new QActionGroup(this);

    auto make = [this](const QString &icon, const QString &text, MapView::Tool tool, const QKeySequence &key) {
        QAction *a = m_tools->addAction(toolIcon(icon), text);
        a->setCheckable(true);
        a->setShortcut(key);
        a->setToolTip(QString("%1 (%2)").arg(text, key.toString(QKeySequence::NativeText)));
        m_toolGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, tool] { m_view->setTool(tool); });
        return a;
    };
    m_drawLand = make("land", tr("Draw land"), MapView::DrawLand, QKeySequence(Qt::Key_D));
    m_eraseLand = make("sea", tr("Make sea"), MapView::EraseLand, QKeySequence(Qt::Key_E));
    m_drawBorder = make("border", tr("Draw border"), MapView::DrawBorder, QKeySequence(Qt::Key_B));
    m_eraseBorder = make("noborder", tr("Remove border"), MapView::EraseBorder, QKeySequence(Qt::Key_R));
    m_fill = make("fill", tr("Fill area"), MapView::Fill, QKeySequence(Qt::Key_F));
    m_pick = make("pick", tr("Choose country"), MapView::Pick, QKeySequence(Qt::Key_C));
    m_tools->addSeparator();
    m_tools->addAction(tr("Undo"), this, &MainWindow::undo);
    m_tools->addAction(tr("Whole map"), m_view, &MapView::zoomToFit);
    addToolBar(Qt::TopToolBarArea, m_tools);
}

void MainWindow::buildSidePanel()
{
    auto *dock = new QDockWidget(tr("Your world"));
    dock->setObjectName("panel");
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto *w = new QWidget;
    auto *lay = new QVBoxLayout(w);

    m_panelTitle = new QLabel;
    QFont f = m_panelTitle->font();
    f.setPointSizeF(f.pointSizeF() * 1.3);
    f.setBold(true);
    m_panelTitle->setFont(f);
    m_panelHelp = new QLabel;
    m_panelHelp->setWordWrap(true);

    m_list = new QListWidget;
    m_list->setIconSize(QSize(36, 24));
    m_list->setSpacing(1);

    m_details = new QWidget;
    auto *dl = new QVBoxLayout(m_details);
    dl->setContentsMargins(0, 0, 0, 0);
    auto *nameRow = new QHBoxLayout;
    m_colorButton = new QToolButton;
    m_colorButton->setToolTip(tr("Colour on the map"));
    m_colorButton->setIconSize(QSize(22, 22));
    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText(tr("Name"));
    nameRow->addWidget(m_colorButton);
    nameRow->addWidget(m_nameEdit, 1);
    dl->addLayout(nameRow);
    m_flagPreview = new QLabel;
    m_flagPreview->setAlignment(Qt::AlignCenter);
    m_flagPreview->setMinimumHeight(110);
    dl->addWidget(m_flagPreview);
    m_flagButton = new QPushButton(tr("🎨 Design flag…"));
    m_flagButton->setMinimumHeight(32);
    dl->addWidget(m_flagButton);

    auto *buttons = new QHBoxLayout;
    m_newButton = new QPushButton;
    m_deleteButton = new QPushButton(tr("Delete"));
    buttons->addWidget(m_newButton);
    buttons->addWidget(m_deleteButton);

    lay->addWidget(m_panelTitle);
    lay->addWidget(m_panelHelp);
    lay->addWidget(m_list, 1);
    lay->addWidget(m_details);
    lay->addLayout(buttons);
    lay->addStretch(0); // takes the free space when the list is hidden

    dock->setWidget(w);
    dock->setMinimumWidth(280);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_refreshing || row < 0)
            return;
        if (const int id = m_list->item(row)->data(Qt::UserRole).toInt())
            selectId(id);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (m_view->stage() >= MapView::CountryStage)
            designFlag(item->data(Qt::UserRole).toInt());
    });
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &MainWindow::renameSelected);
    connect(m_colorButton, &QToolButton::clicked, this, &MainWindow::recolorSelected);
    connect(m_deleteButton, &QPushButton::clicked, this, &MainWindow::deleteSelected);
    connect(m_newButton, &QPushButton::clicked, this, &MainWindow::newEntity);
    connect(m_flagButton, &QPushButton::clicked, this, [this] { designFlag(m_view->selectedCountry()); });
}

// ---- Stages -------------------------------------------------------------------

void MainWindow::setStage(MapView::Stage stage)
{
    m_view->setStage(stage);
    m_stageButtons->button(int(stage))->setChecked(true);
    m_nextButton->setEnabled(stage != MapView::FlagStage);

    const bool land = stage == MapView::LandStage;
    const bool divide = stage == MapView::ContinentStage || stage == MapView::CountryStage;
    m_drawLand->setVisible(land);
    m_eraseLand->setVisible(land);
    m_drawBorder->setVisible(divide);
    m_eraseBorder->setVisible(divide);
    m_fill->setVisible(divide);
    m_pick->setVisible(stage == MapView::FlagStage);
    (land ? m_drawLand : divide ? m_drawBorder : m_pick)->setChecked(true);

    refreshSidePanel();
}

// ---- Side panel ---------------------------------------------------------------

int MainWindow::selectedId() const
{
    return m_view->stage() == MapView::ContinentStage ? m_view->selectedContinent() : m_view->selectedCountry();
}

void MainWindow::selectId(int id)
{
    if (m_view->stage() == MapView::ContinentStage)
        m_view->setSelectedContinent(id);
    else
        m_view->setSelectedCountry(id);
    refreshDetails();
}

void MainWindow::refreshSidePanel()
{
    const MapView::Stage s = m_view->stage();
    switch (s) {
    case MapView::LandStage:
        m_panelTitle->setText(tr("Draw your world"));
        m_panelHelp->setText(tr("Draw the coastlines of your own world. Every shape you draw becomes land; "
                                "shapes that touch grow together. Use \"Make sea\" (or the right mouse button) "
                                "to carve out bays, lakes and straits.\n\nWhen you like your land, press Next step."));
        break;
    case MapView::ContinentStage:
        m_panelTitle->setText(tr("Continents"));
        m_panelHelp->setText(tr("1. Draw border lines to cut the land into pieces.\n"
                                "2. Use the bucket to click each piece: it becomes a continent.\n"
                                "Shift+click adds an island to the selected continent."));
        m_newButton->setText(tr("+ Empty continent"));
        break;
    case MapView::CountryStage:
        m_panelTitle->setText(tr("Countries"));
        m_panelHelp->setText(tr("Draw borders inside the continents, then click each piece with the bucket "
                                "to make a country. Every new country gets a surprise flag!"));
        m_newButton->setText(tr("+ Empty country"));
        break;
    case MapView::FlagStage:
        m_panelTitle->setText(tr("Names & flags"));
        m_panelHelp->setText(tr("Click a country on the map or in the list, give it a name and design its flag. "
                                "Double-click opens the flag designer."));
        break;
    }
    m_list->setVisible(s != MapView::LandStage);
    m_newButton->setVisible(s == MapView::ContinentStage || s == MapView::CountryStage);
    m_deleteButton->setVisible(s != MapView::LandStage);
    refreshList();
    refreshDetails();
}

void MainWindow::refreshList()
{
    m_refreshing = true;
    m_list->clear();
    const MapView::Stage s = m_view->stage();
    int current = -1;
    if (s == MapView::ContinentStage) {
        for (const Continent &c : m_world.continents) {
            auto *item = new QListWidgetItem(swatch(c.color, 36, 24), c.name);
            item->setData(Qt::UserRole, c.id);
            m_list->addItem(item);
            if (c.id == m_view->selectedContinent())
                current = m_list->count() - 1;
        }
    } else if (s == MapView::CountryStage || s == MapView::FlagStage) {
        // Group countries by continent.
        QList<int> order;
        for (const Continent &c : m_world.continents)
            order << c.id;
        order << 0;
        for (int contId : order) {
            bool header = false;
            for (const Country &c : m_world.countries) {
                if (c.continentId != contId && !(contId == 0 && !m_world.continent(c.continentId)))
                    continue;
                if (!header) {
                    const Continent *cont = m_world.continent(contId);
                    auto *h = new QListWidgetItem(cont ? cont->name.toUpper() : tr("NO CONTINENT"));
                    h->setFlags(Qt::ItemIsEnabled); // not selectable, but keeps its colour
                    QFont f = h->font();
                    f.setBold(true);
                    h->setFont(f);
                    if (cont)
                        h->setIcon(swatch(cont->color, 12, 12));
                    m_list->addItem(h);
                    header = true;
                }
                auto *item = new QListWidgetItem(QIcon(c.flag.toPixmap(36)), c.name);
                item->setData(Qt::UserRole, c.id);
                m_list->addItem(item);
                if (c.id == m_view->selectedCountry())
                    current = m_list->count() - 1;
            }
        }
    }
    m_list->setCurrentRow(current);
    m_refreshing = false;
}

void MainWindow::refreshDetails()
{
    m_refreshing = true;
    const MapView::Stage s = m_view->stage();
    const int id = selectedId();
    QString name;
    QColor color;
    const Country *country = nullptr;
    if (s == MapView::ContinentStage) {
        if (const Continent *c = m_world.continent(id))
            name = c->name, color = c->color;
    } else if (s != MapView::LandStage) {
        country = m_world.country(id);
        if (country)
            name = country->name, color = country->color;
    }
    const bool has = s != MapView::LandStage && !name.isNull();
    m_details->setVisible(has);
    m_deleteButton->setEnabled(has);
    if (has) {
        m_nameEdit->setText(name);
        m_colorButton->setIcon(swatch(color));
        const bool showFlag = country != nullptr;
        m_flagPreview->setVisible(showFlag);
        m_flagButton->setVisible(showFlag);
        if (country)
            m_flagPreview->setPixmap(country->flag.toPixmap(150));
    }
    // Keep the list selection in line with the map.
    for (int i = 0; i < m_list->count(); ++i)
        if (id && m_list->item(i)->data(Qt::UserRole).toInt() == id) {
            m_list->setCurrentRow(i);
            break;
        }
    m_refreshing = false;
}

void MainWindow::renameSelected()
{
    if (m_refreshing)
        return;
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        return;
    const int id = selectedId();
    if (m_view->stage() == MapView::ContinentStage) {
        Continent *c = m_world.continent(id);
        if (!c || c->name == name)
            return;
        saveUndo();
        m_world.continent(id)->name = name;
    } else {
        Country *c = m_world.country(id);
        if (!c || c->name == name)
            return;
        saveUndo();
        m_world.country(id)->name = name;
    }
    m_view->worldChanged();
    refreshList();
    markEdited();
}

void MainWindow::recolorSelected()
{
    const int id = selectedId();
    const bool cont = m_view->stage() == MapView::ContinentStage;
    const QColor old = cont ? (m_world.continent(id) ? m_world.continent(id)->color : QColor())
                            : (m_world.country(id) ? m_world.country(id)->color : QColor());
    if (!old.isValid())
        return;
    const QColor c = QColorDialog::getColor(old, this, tr("Colour on the map"));
    if (!c.isValid() || c == old)
        return;
    saveUndo();
    if (cont)
        m_world.continent(id)->color = c;
    else
        m_world.country(id)->color = c;
    m_view->worldChanged();
    refreshSidePanel();
    markEdited();
}

void MainWindow::deleteSelected()
{
    const int id = selectedId();
    if (!id)
        return;
    saveUndo();
    if (m_view->stage() == MapView::ContinentStage)
        m_world.removeContinent(id);
    else
        m_world.removeCountry(id);
    m_view->worldChanged();
    refreshSidePanel();
    markEdited();
}

void MainWindow::newEntity()
{
    saveUndo();
    int id;
    if (m_view->stage() == MapView::ContinentStage) {
        id = m_world.newContinent();
        m_view->setSelectedContinent(id);
    } else {
        id = m_world.newCountry(0);
        m_view->setSelectedCountry(id);
    }
    m_view->setTool(MapView::Fill);
    m_fill->setChecked(true);
    m_hint->setText(tr("Now Shift+click areas on the map to add them to it."));
    refreshSidePanel();
    markEdited();
}

void MainWindow::designFlag(int countryId)
{
    Country *c = m_world.country(countryId);
    if (!c)
        return;
    m_view->setSelectedCountry(countryId);
    FlagEditor dlg(c->name, c->flag, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    saveUndo();
    c = m_world.country(countryId);
    c->flag = dlg.flag();
    if (!dlg.countryName().isEmpty())
        c->name = dlg.countryName();
    m_view->worldChanged();
    refreshSidePanel();
    markEdited();
}

// ---- Undo ---------------------------------------------------------------------

void MainWindow::saveUndo()
{
    m_undo.append(m_world);
    if (m_undo.size() > kMaxUndo)
        m_undo.removeFirst();
    m_redo.clear();
    m_undoAct->setEnabled(true);
    m_redoAct->setEnabled(false);
}

void MainWindow::undo()
{
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_world);
    m_world = m_undo.takeLast();
    m_undoAct->setEnabled(!m_undo.isEmpty());
    m_redoAct->setEnabled(true);
    m_view->worldChanged();
    refreshSidePanel();
    markEdited();
}

void MainWindow::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_world);
    m_world = m_redo.takeLast();
    m_undoAct->setEnabled(true);
    m_redoAct->setEnabled(!m_redo.isEmpty());
    m_view->worldChanged();
    refreshSidePanel();
    markEdited();
}

void MainWindow::markEdited()
{
    m_modified = true;
    refreshTitle();
    refreshList();
    refreshDetails();
}

void MainWindow::refreshTitle()
{
    const QString name = m_filePath.isEmpty() ? tr("New world") : QFileInfo(m_filePath).completeBaseName();
    setWindowTitle(QString("%1%2 – Vlaggenspel").arg(name, m_modified ? "*" : ""));
}

// ---- Files --------------------------------------------------------------------

bool MainWindow::maybeSave()
{
    if (!m_modified)
        return true;
    const auto answer = QMessageBox::question(this, tr("Save your world?"),
                                              tr("Your world has changes that are not saved yet. Save them?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return saveWorld();
    return answer == QMessageBox::Discard;
}

void MainWindow::newWorld()
{
    if (!maybeSave())
        return;
    m_world = World();
    m_undo.clear();
    m_redo.clear();
    m_undoAct->setEnabled(false);
    m_redoAct->setEnabled(false);
    m_filePath.clear();
    m_modified = false;
    setStage(MapView::LandStage);
    m_view->worldChanged();
    m_view->zoomToFit();
    refreshTitle();
}

void MainWindow::openWorld()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open world"), QString(),
                                                      tr("Vlaggenspel worlds (*.vlag);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open world"), tr("Could not open %1.").arg(path));
        return;
    }
    QString error;
    World w;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!World::fromJson(doc.object(), w, &error)) {
        QMessageBox::warning(this, tr("Open world"), error);
        return;
    }
    m_world = w;
    m_undo.clear();
    m_redo.clear();
    m_undoAct->setEnabled(false);
    m_redoAct->setEnabled(false);
    m_filePath = path;
    m_modified = false;
    setStage(m_world.countries.isEmpty()
                 ? (m_world.continents.isEmpty() ? MapView::LandStage : MapView::ContinentStage)
                 : MapView::FlagStage);
    m_view->worldChanged();
    m_view->zoomToFit();
    refreshTitle();
}

bool MainWindow::saveWorld()
{
    if (m_filePath.isEmpty())
        return saveWorldAs();
    return writeFile(m_filePath);
}

bool MainWindow::saveWorldAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save world"), tr("My world.vlag"),
                                                tr("Vlaggenspel worlds (*.vlag)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".vlag", Qt::CaseInsensitive))
        path += ".vlag";
    return writeFile(path);
}

bool MainWindow::writeFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Save world"), tr("Could not write %1.").arg(path));
        return false;
    }
    f.write(QJsonDocument(m_world.toJson()).toJson(QJsonDocument::Compact));
    m_filePath = path;
    m_modified = false;
    refreshTitle();
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 3000);
    return true;
}

void MainWindow::exportPng()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export map"), tr("My world.png"), tr("PNG pictures (*.png)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".png", Qt::CaseInsensitive))
        path += ".png";
    if (!m_view->exportImage().save(path))
        QMessageBox::warning(this, tr("Export map"), tr("Could not write %1.").arg(path));
}

void MainWindow::showHelp()
{
    QMessageBox::information(this, tr("How to play"),
        tr("<h3>Make your own world</h3>"
           "<p><b>1. Draw the land.</b> Hold the left mouse button and draw the outline of a piece of land. "
           "Let go and it is filled in. Draw over the coast to make it bigger. "
           "Right mouse button (or \"Make sea\") cuts sea out of the land.</p>"
           "<p><b>2. Make continents.</b> Draw border lines across the land, from sea to sea. "
           "Then click each piece with the bucket to turn it into a continent.</p>"
           "<p><b>3. Make countries.</b> Do the same inside each continent. "
           "Every country gets a colour and a surprise flag.</p>"
           "<p><b>4. Names &amp; flags.</b> Click a country, type its name and press "
           "<i>Design flag</i>. Pick a colour and click the stripes and shapes to paint them.</p>"
           "<p>Scroll to zoom, drag with the middle (or right) mouse button to move the map. "
           "Ctrl+Z undoes a mistake.</p>"));
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (maybeSave())
        e->accept();
    else
        e->ignore();
}
