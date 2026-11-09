#pragma once

#include "mapview.h"
#include "world.h"

#include <QMainWindow>

class QAction;
class QActionGroup;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolBar;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildMenus();
    void buildStageBar();
    void buildToolBar();
    void buildSidePanel();

    void setStage(MapView::Stage stage);
    void refreshSidePanel();
    void refreshList();
    void refreshDetails();
    void refreshTitle();

    void saveUndo();
    void undo();
    void redo();
    void markEdited();

    void newWorld();
    void openWorld();
    bool saveWorld();
    bool saveWorldAs();
    bool writeFile(const QString &path);
    void exportPng();
    bool maybeSave();
    void showHelp();

    int selectedId() const;
    void selectId(int id);
    void renameSelected();
    void recolorSelected();
    void deleteSelected();
    void newEntity();
    void designFlag(int countryId);

    World m_world;
    QList<World> m_undo;
    QList<World> m_redo;
    QString m_filePath;
    bool m_modified = false;

    MapView *m_view;
    QButtonGroup *m_stageButtons;
    QPushButton *m_nextButton;
    QToolBar *m_tools;
    QActionGroup *m_toolGroup;
    QAction *m_drawLand, *m_eraseLand, *m_drawBorder, *m_eraseBorder, *m_fill, *m_pick;
    QAction *m_undoAct, *m_redoAct;

    QLabel *m_panelTitle;
    QLabel *m_panelHelp;
    QListWidget *m_list;
    QWidget *m_details;
    QLineEdit *m_nameEdit;
    QToolButton *m_colorButton;
    QPushButton *m_newButton;
    QPushButton *m_deleteButton;
    QLabel *m_flagPreview;
    QPushButton *m_flagButton;
    QLabel *m_hint;
    bool m_refreshing = false;
};
