TEMPLATE = app
TARGET = motion
CONFIG += console
CONFIG += c++17
CONFIG -= app_bundle
#CONFIG -= qt
QT += core #gui widgets needed for QApplication


DEFINES -= DEBUG_BUILD

SOURCES += \
    avreadwrite.cpp \
    backgroundsubtraction.cpp \
    motion-detector.cpp \
    motion-fast.cpp \
    test/avreadwrite-test.cpp \
    test/show-diag-pics.cpp \
    time-stamp.cpp

LIBS += -lavformat \
        -lavcodec \
        -lavutil \
        -lopencv_core \
        -lopencv_highgui \
        -lopencv_imgcodecs \
        -lopencv_imgproc \
        -lopencv_videoio \
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
    avreadwrite.h \
    backgroundsubtraction.h \
    circularbuffer.h \
    motion-detector.h \
    perfcounter.h \
    safebuffer.h \
    time-stamp.h

INSTALLS = target
target.path = /home/pi
