# qsoundcloud (https://github.com/marxoft/qsoundcloud) as a static library that
# is linked into the app, so no separate DLL is built or deployed. The sources
# are the git submodule external/qsoundcloud (git submodule update --init).
#
# A static library rather than compiling the sources into app.pro: the app has
# its own resourcesrequest.h, and two moc_resourcesrequest.cpp in one Symbian
# MMP collide.
#
# qsoundcloud's json.cpp is left out: it is the same QtJson as
# app/src/base/json.cpp, which the app provides.
#
# The app includes <qsoundcloud/*.h>: see external/include/qsoundcloud/.

TEMPLATE = lib
TARGET = qsoundcloud
CONFIG += staticlib

QT += network
QT -= gui

DEFINES += QSOUNDCLOUD_STATIC_LIBRARY

SRC = ../qsoundcloud/src
INCLUDEPATH += $$SRC

HEADERS += \
    $$SRC/authenticationrequest.h \
    $$SRC/json.h \
    $$SRC/model.h \
    $$SRC/model_p.h \
    $$SRC/qsoundcloud_global.h \
    $$SRC/request.h \
    $$SRC/request_p.h \
    $$SRC/resourcesmodel.h \
    $$SRC/resourcesrequest.h \
    $$SRC/streamsmodel.h \
    $$SRC/streamsrequest.h \
    $$SRC/urls.h

SOURCES += \
    $$SRC/authenticationrequest.cpp \
    $$SRC/model.cpp \
    $$SRC/request.cpp \
    $$SRC/resourcesmodel.cpp \
    $$SRC/resourcesrequest.cpp \
    $$SRC/streamsmodel.cpp \
    $$SRC/streamsrequest.cpp
