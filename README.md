#MusiKloud2

MusiKloud2 is a SoundCloud client and music player that can be extended via plugins.

Features include:

    * Browse and search tracks, sets and users.
    * Play and download tracks.
    * Folder-based playback of local music.
    * Support for multiple SoundCloud accounts.
    * Support for user-defined credentials for SoundCloud.
    * Support for additional services via plugins.

## Building for Symbian

Needs the Qt SDK at `C:\QtSDK` (Symbian^3 Qt 4.7.4 target) and the qsoundcloud submodule:

    git submodule update --init
    build_symbian.bat            (release, default)
    build_symbian.bat debug

This produces an unsigned `musikloud2.sis` in the repo root. qsoundcloud is built from
`external/qsoundcloud` as a static library (`external/qsoundcloud_lib`) and linked into
the app, so nothing extra has to be installed on the phone. The build is in-source:
generated files land next to the `.pro` files and are gitignored.
