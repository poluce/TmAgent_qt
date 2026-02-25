TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += \
    tmagent_app \
    tmagent_cli \
    tmagent_log

tmagent_app.file = app.pro
tmagent_cli.file = cli.pro
tmagent_log.file = tmagent-log/tmagent-log.pro
