/****************************************************************************
** Meta object code from reading C++ file 'LspClient.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.14.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../core/lsp/LspClient.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'LspClient.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.14.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_LspClient_t {
    QByteArrayData data[26];
    char stringdata0[304];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_LspClient_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_LspClient_t qt_meta_stringdata_LspClient = {
    {
QT_MOC_LITERAL(0, 0, 9), // "LspClient"
QT_MOC_LITERAL(1, 10, 11), // "initialized"
QT_MOC_LITERAL(2, 22, 0), // ""
QT_MOC_LITERAL(3, 23, 12), // "stateChanged"
QT_MOC_LITERAL(4, 36, 16), // "LspClient::State"
QT_MOC_LITERAL(5, 53, 5), // "state"
QT_MOC_LITERAL(6, 59, 19), // "diagnosticsReceived"
QT_MOC_LITERAL(7, 79, 3), // "uri"
QT_MOC_LITERAL(8, 83, 22), // "QList<Lsp::Diagnostic>"
QT_MOC_LITERAL(9, 106, 11), // "diagnostics"
QT_MOC_LITERAL(10, 118, 13), // "errorOccurred"
QT_MOC_LITERAL(11, 132, 5), // "error"
QT_MOC_LITERAL(12, 138, 17), // "onMessageReceived"
QT_MOC_LITERAL(13, 156, 7), // "message"
QT_MOC_LITERAL(14, 164, 16), // "onTransportError"
QT_MOC_LITERAL(15, 181, 17), // "onProcessFinished"
QT_MOC_LITERAL(16, 199, 8), // "exitCode"
QT_MOC_LITERAL(17, 208, 20), // "QProcess::ExitStatus"
QT_MOC_LITERAL(18, 229, 6), // "status"
QT_MOC_LITERAL(19, 236, 5), // "State"
QT_MOC_LITERAL(20, 242, 10), // "NotStarted"
QT_MOC_LITERAL(21, 253, 8), // "Starting"
QT_MOC_LITERAL(22, 262, 12), // "Initializing"
QT_MOC_LITERAL(23, 275, 7), // "Running"
QT_MOC_LITERAL(24, 283, 12), // "ShuttingDown"
QT_MOC_LITERAL(25, 296, 7) // "Stopped"

    },
    "LspClient\0initialized\0\0stateChanged\0"
    "LspClient::State\0state\0diagnosticsReceived\0"
    "uri\0QList<Lsp::Diagnostic>\0diagnostics\0"
    "errorOccurred\0error\0onMessageReceived\0"
    "message\0onTransportError\0onProcessFinished\0"
    "exitCode\0QProcess::ExitStatus\0status\0"
    "State\0NotStarted\0Starting\0Initializing\0"
    "Running\0ShuttingDown\0Stopped"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_LspClient[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       1,   72, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   49,    2, 0x06 /* Public */,
       3,    1,   50,    2, 0x06 /* Public */,
       6,    2,   53,    2, 0x06 /* Public */,
      10,    1,   58,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      12,    1,   61,    2, 0x08 /* Private */,
      14,    1,   64,    2, 0x08 /* Private */,
      15,    2,   67,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 4,    5,
    QMetaType::Void, QMetaType::QString, 0x80000000 | 8,    7,    9,
    QMetaType::Void, QMetaType::QString,   11,

 // slots: parameters
    QMetaType::Void, QMetaType::QJsonObject,   13,
    QMetaType::Void, QMetaType::QString,   11,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 17,   16,   18,

 // enums: name, alias, flags, count, data
      19,   19, 0x2,    6,   77,

 // enum data: key, value
      20, uint(LspClient::State::NotStarted),
      21, uint(LspClient::State::Starting),
      22, uint(LspClient::State::Initializing),
      23, uint(LspClient::State::Running),
      24, uint(LspClient::State::ShuttingDown),
      25, uint(LspClient::State::Stopped),

       0        // eod
};

void LspClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<LspClient *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->initialized(); break;
        case 1: _t->stateChanged((*reinterpret_cast< LspClient::State(*)>(_a[1]))); break;
        case 2: _t->diagnosticsReceived((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QList<Lsp::Diagnostic>(*)>(_a[2]))); break;
        case 3: _t->errorOccurred((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->onMessageReceived((*reinterpret_cast< const QJsonObject(*)>(_a[1]))); break;
        case 5: _t->onTransportError((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 6: _t->onProcessFinished((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< QProcess::ExitStatus(*)>(_a[2]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (LspClient::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&LspClient::initialized)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (LspClient::*)(LspClient::State );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&LspClient::stateChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (LspClient::*)(const QString & , const QList<Lsp::Diagnostic> & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&LspClient::diagnosticsReceived)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (LspClient::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&LspClient::errorOccurred)) {
                *result = 3;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject LspClient::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_LspClient.data,
    qt_meta_data_LspClient,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *LspClient::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *LspClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_LspClient.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int LspClient::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void LspClient::initialized()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void LspClient::stateChanged(LspClient::State _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void LspClient::diagnosticsReceived(const QString & _t1, const QList<Lsp::Diagnostic> & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void LspClient::errorOccurred(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
