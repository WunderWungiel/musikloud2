TEMPLATE = subdirs

symbian {
    # qsoundcloud is built from the external/qsoundcloud submodule as a static
    # library and linked into the app (external/qsoundcloud_lib).
    SUBDIRS += external/qsoundcloud_lib
    app.depends += external/qsoundcloud_lib
}

SUBDIRS += \
    app

contains(MEEGO_EDITION,harmattan) {
    SUBDIRS += \
        ../qsoundcloud/src

    app.depends += \
        ../qsoundcloud/src
}
