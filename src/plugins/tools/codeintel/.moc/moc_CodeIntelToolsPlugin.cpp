/****************************************************************************
** Meta object code from reading C++ file 'CodeIntelToolsPlugin.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.14.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../CodeIntelToolsPlugin.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qplugin.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'CodeIntelToolsPlugin.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.14.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_CodeIntelToolsPlugin_t {
    QByteArrayData data[1];
    char stringdata0[21];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_CodeIntelToolsPlugin_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_CodeIntelToolsPlugin_t qt_meta_stringdata_CodeIntelToolsPlugin = {
    {
QT_MOC_LITERAL(0, 0, 20) // "CodeIntelToolsPlugin"

    },
    "CodeIntelToolsPlugin"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_CodeIntelToolsPlugin[] = {

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

void CodeIntelToolsPlugin::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    Q_UNUSED(_o);
    Q_UNUSED(_id);
    Q_UNUSED(_c);
    Q_UNUSED(_a);
}

QT_INIT_METAOBJECT const QMetaObject CodeIntelToolsPlugin::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_CodeIntelToolsPlugin.data,
    qt_meta_data_CodeIntelToolsPlugin,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *CodeIntelToolsPlugin::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CodeIntelToolsPlugin::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CodeIntelToolsPlugin.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "TmAgent::IToolPlugin"))
        return static_cast< TmAgent::IToolPlugin*>(this);
    if (!strcmp(_clname, "org.tmagent.ToolPlugin/1.0"))
        return static_cast< TmAgent::IToolPlugin*>(this);
    return QObject::qt_metacast(_clname);
}

int CodeIntelToolsPlugin::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
    0x03,  0x74,  'C',  'o',  'd',  'e',  'I',  'n', 
    't',  'e',  'l',  'T',  'o',  'o',  'l',  's', 
    'P',  'l',  'u',  'g',  'i',  'n', 
    // "MetaData"
    0x04,  0xa4,  0x68,  'c',  'a',  't',  'e',  'g', 
    'o',  'r',  'y',  0x6a,  'c',  'o',  'd',  'e', 
    '_',  'i',  'n',  't',  'e',  'l',  0x6c,  'd', 
    'i',  's',  'p',  'l',  'a',  'y',  '_',  'n', 
    'a',  'm',  'e',  0x72,  uchar('\xe4'), uchar('\xbb'), uchar('\xa3'), uchar('\xe7'),
    uchar('\xa0'), uchar('\x81'), uchar('\xe6'), uchar('\x99'), uchar('\xba'), uchar('\xe8'), uchar('\x83'), uchar('\xbd'),
    uchar('\xe5'), uchar('\xb7'), uchar('\xa5'), uchar('\xe5'), uchar('\x85'), uchar('\xb7'), 0x69,  'p', 
    'l',  'u',  'g',  'i',  'n',  '_',  'i',  'd', 
    0x70,  'c',  'o',  'd',  'e',  '_',  'i',  'n', 
    't',  'e',  'l',  '_',  't',  'o',  'o',  'l', 
    's',  0x67,  'v',  'e',  'r',  's',  'i',  'o', 
    'n',  0x65,  '1',  '.',  '0',  '.',  '0', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN(CodeIntelToolsPlugin, CodeIntelToolsPlugin)

QT_WARNING_POP
QT_END_MOC_NAMESPACE
