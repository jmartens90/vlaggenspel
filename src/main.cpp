#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Vlaggenspel");
    QApplication::setOrganizationName("Vlaggenspel");

    MainWindow window;
    window.show();
    return app.exec();
}
