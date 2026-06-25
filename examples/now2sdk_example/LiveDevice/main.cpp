#include "mainwindow.h"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{

    qputenv("QSG_RHI_BACKEND", "opengl");
    
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    
    qputenv("QSG_RHI_BACKEND", "opengl");
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    int result = a.exec();
    
    return result;
}