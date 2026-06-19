#include "mainwindow.h"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    // --- SOFTWARE ARMOR: Hardware and Driver Compatibility ---
    
    // Pin Qt RHI Layer to OpenGL (for AMD/Wayland/X11 compatibility)
    qputenv("QSG_RHI_BACKEND", "opengl");
    
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    
    qputenv("QSG_RHI_BACKEND", "opengl");
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");

    // Enable resource sharing (GL context sharing between threads)
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);


    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    int result = a.exec();
    
    return result;
}