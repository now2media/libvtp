#include <QApplication>
#include "SenderWindow.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    
    SenderWindow w;
    w.show();
    
    return a.exec();
}
