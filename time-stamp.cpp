#include "time-stamp.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>


// Functions //////////////////////////////////////////////////////////////////
/// \brief getTimeStamp
/// \param resolution
/// \param currentTime (default: time_since_epoch = 0)
/// \return time stamp as string
///
std::string getTimeStamp(TimeResolution resolution, TimePoint currentTime) {
    auto tp = currentTime;
    // currentTime = 0 (default) -> use system clock -> now
    if (tp.time_since_epoch().count() == 0) {
        tp = std::chrono::system_clock::now();
    }

    // time stamp in std::chrono format 
    auto tpMicroSec = std::chrono::duration_cast<std::chrono::microseconds>
            (tp.time_since_epoch());
    auto tpMilliSec =  std::chrono::duration_cast<std::chrono::milliseconds>(tpMicroSec);
    auto tpSec = std::chrono::duration_cast<std::chrono::seconds>(tpMicroSec);

    // convert seconds to time_t for pretty printing with put_time
    std::time_t tpSecTimeT = tpSec.count();

    std::stringstream timeStamp;
    // print with blanks (better reading in console)
    if (resolution == TimeResolution::ms || resolution == TimeResolution::sec) {
        timeStamp << std::put_time(std::localtime(&tpSecTimeT), "%F %T");
    // print w/o blanks (use in file name)
    } else {
        timeStamp << std::put_time(std::localtime(&tpSecTimeT), "%F_%Hh%Mm%Ss");
    }

    if (resolution == TimeResolution::ms) {
        long tpMilliSecRemainder = tpMilliSec.count() % 1000;
        timeStamp << "." << tpMilliSecRemainder;
    }

    // avoid . as ms separator in order to use time stamp as opencv persistence key
    if (resolution == TimeResolution::ms_NoBlank) {
        long tpMilliSecRemainder = tpMilliSec.count() % 1000;
        timeStamp << tpMilliSecRemainder << "ms";
    }

    // add microseconds
    if (resolution == TimeResolution::micSec_NoBlank) {

        long tpMilliSecRemainder = tpMilliSec.count() % 1000;
        long tpMicroSecRemainder = tpMicroSec.count() % 1000;
        timeStamp << tpMilliSecRemainder << "ms" << tpMicroSecRemainder;
    }

    return timeStamp.str();
}


template <typename T> std::chrono::milliseconds asMilliSec(T duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration);
}


long elapsedMs(std::chrono::system_clock::time_point startTimePoint) {
    std::chrono::system_clock::time_point endTimePiont = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(endTimePiont - startTimePoint).count();
}

long elapsedMicroSec(std::chrono::system_clock::time_point startTimePoint) {
    std::chrono::system_clock::time_point endTimePiont = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(endTimePiont - startTimePoint).count();
}
