TEMPLATE = app
TARGET = libav
CONFIG += console
CONFIG += c++14
CONFIG -= app_bundle
CONFIG -= qt


DEFINES += DEBUG_BUILD

SOURCES += \
    backgroundsubtraction.cpp \
    decode-buffer.cpp \
    decode.c \
    libavreadwrite.cpp \
    motion-detector.cpp \
    test.cpp \
    time-stamp.cpp

LIBS += -lavformat \
        -lavcodec \
        -lavutil \
        -lopencv_core \
        -lopencv_highgui \
        -lopencv_imgproc \
        -lpthread

contains(QT_ARCH, "arm") {
    message("ARM cross")
    # QMAKE_LFLAGS += --verbose
    # QMAKE_LFLAGS += -Wl,-rpath-link,/lib/arm-linux-gnueabihf
    message("linker: $$QMAKE_LINK")
    message("flags: $$QMAKE_LFLAGS")
    message("default lib dirs: $$QMAKE_DEFAULT_LIBDIRS")
    INCLUDEPATH += /home/holger/app-dev/pi4-qt/sysroot/usr/local/include
} else {
    message("Desktop")
}


HEADERS += \
    backgroundsubtraction.h \
    libavreadwrite.h \
    motion-detector.h \
    safequeque.h \
    time-stamp.h

INSTALLS = target
target.path = /home/pi
