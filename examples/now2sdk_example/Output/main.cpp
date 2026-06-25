#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QSurfaceFormat>

int main(int argc, char *argv[]) {

    qputenv("QSG_RHI_BACKEND", "opengl");
    
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    
    qputenv("QSG_RHI_BACKEND", "opengl");
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication a(argc, argv);

    a.setStyle(QStyleFactory::create("Fusion"));
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(13, 14, 18));
    darkPalette.setColor(QPalette::WindowText, QColor(226, 232, 240));
    darkPalette.setColor(QPalette::Base, QColor(17, 24, 39));
    darkPalette.setColor(QPalette::AlternateBase, QColor(31, 41, 55));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, QColor(226, 232, 240));
    darkPalette.setColor(QPalette::Button, QColor(31, 41, 55));
    darkPalette.setColor(QPalette::ButtonText, QColor(243, 244, 246));
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(56, 189, 248));
    darkPalette.setColor(QPalette::Highlight, QColor(2, 132, 199));
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    a.setPalette(darkPalette);
    
    MainWindow w;
    w.show();
    return a.exec();
}
