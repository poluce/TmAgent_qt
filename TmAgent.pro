TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += \
    tmagent_app \
    tmagent_log

tmagent_app.file = TmAgentApp.pro
tmagent_log.file = tools/tmagent-log/tmagent-log.pro
