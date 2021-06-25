#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#include <chrono>
#include <string>

enum class TimeResolution {ms, sec, micSec_NoBlank, ms_NoBlank, sec_NoBlank};

typedef std::chrono::time_point<std::chrono::system_clock> TimePoint;


std::string getTimeStamp(TimeResolution resolution,
                         TimePoint currentTime = TimePoint()); // default = 0

inline std::string getTimeStampMs() {
    return getTimeStamp(TimeResolution::ms);
};

template <typename T> std::chrono::milliseconds asMilliSec(T duration);

long elapsedMs(std::chrono::system_clock::time_point startTimePoint);

long elapsedMicroSec(std::chrono::system_clock::time_point startTimePoint);

#endif // TIMESTAMP_H
