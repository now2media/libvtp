#ifndef NOW2SDK_GLOBAL_H
#define NOW2SDK_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(NOW2SDK_LIBRARY)
#define NOW2SDK_EXPORT Q_DECL_EXPORT
#else
#define NOW2SDK_EXPORT Q_DECL_IMPORT
#endif

#endif // NOW2SDK_GLOBAL_H
