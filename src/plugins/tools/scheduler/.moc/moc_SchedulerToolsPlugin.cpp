/****************************************************************************
** Meta object code from reading C++ file 'SchedulerToolsPlugin.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.14.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../SchedulerToolsPlugin.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qplugin.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'SchedulerToolsPlugin.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.14.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SchedulerToolsPlugin_t {
    QByteArrayData data[1];
    char stringdata0[21];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SchedulerToolsPlugin_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SchedulerToolsPlugin_t qt_meta_stringdata_SchedulerToolsPlugin = {
    {
QT_MOC_LITERAL(0, 0, 20) // "SchedulerToolsPlugin"

    },
    "SchedulerToolsPlugin"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SchedulerToolsPlugin[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

void SchedulerToolsPlugin::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    Q_UNUSED(_o);
    Q_UNUSED(_id);
    Q_UNUSED(_c);
    Q_UNUSED(_a);
}

QT_INIT_METAOBJECT const QMetaObject SchedulerToolsPlugin::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_SchedulerToolsPlugin.data,
    qt_meta_data_SchedulerToolsPlugin,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *SchedulerToolsPlugin::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SchedulerToolsPlugin::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SchedulerToolsPlugin.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "IToolPlugin"))
        return static_cast< IToolPlugin*>(this);
    if (!strcmp(_clname, "org.tmagent.ToolPlugin/1.0"))
        return static_cast< TmAgent::IToolPlugin*>(this);
    return QObject::qt_metacast(_clname);
}

int SchedulerToolsPlugin::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}

QT_PLUGIN_METADATA_SECTION
static constexpr unsigned char qt_pluginMetaData[] = {
    'Q', 'T', 'M', 'E', 'T', 'A', 'D', 'A', 'T', 'A', ' ', '!',
    // metadata version, Qt version, architectural requirements
    0, QT_VERSION_MAJOR, QT_VERSION_MINOR, qPluginArchRequirements(),
    0xbf, 
    // "IID"
    0x02,  0x78,  0x1a,  'o',  'r',  'g',  '.',  't', 
    'm',  'a',  'g',  'e',  'n',  't',  '.',  'T', 
    'o',  'o',  'l',  'P',  'l',  'u',  'g',  'i', 
    'n',  '/',  '1',  '.',  '0', 
    // "className"
    0x03,  0x74,  'S',  'c',  'h',  'e',  'd',  'u', 
    'l',  'e',  'r',  'T',  'o',  'o',  'l',  's', 
    'P',  'l',  'u',  'g',  'i',  'n', 
    // "MetaData"
    0x04,  0xa4,  0x68,  'c',  'a',  't',  'e',  'g', 
    'o',  'r',  'y',  0x69,  's',  'c',  'h',  'e', 
    'd',  'u',  'l',  'e',  'r',  0x6c,  'd',  'i', 
    's',  'p',  'l',  'a',  'y',  '_',  'n',  'a', 
    'm',  'e',  0x72,  uchar('\xe5'), uchar('\xae'), uchar('\x9a'), uchar('\xe6'), uchar('\x97'),
    uchar('\xb6'), uchar('\xe4'), uchar('\xbb'), uchar('\xbb'), uchar('\xe5'), uchar('\x8a'), uchar('\xa1'), uchar('\xe5'),
    uchar('\xb7'), uchar('\xa5'), uchar('\xe5'), uchar('\x85'), uchar('\xb7'), 0x69,  'p',  'l', 
    'u',  'g',  'i',  'n',  '_',  'i',  'd',  0x6f, 
    's',  'c',  'h',  'e',  'd',  'u',  'l',  'e', 
    'r',  '_',  't',  'o',  'o',  'l',  's',  0x67, 
    'v',  'e',  'r',  's',  'i',  'o',  'n',  0x65, 
    '1',  '.',  '0',  '.',  '0', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN(SchedulerToolsPlugin, SchedulerToolsPlugin)

QT_WARNING_POP
QT_END_MOC_NAMESPACE
