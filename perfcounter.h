#ifndef PERFCOUNTER_H
#define PERFCOUNTER_H

#include <algorithm> // max_element
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>  // accumulate
#include <vector>


// TYPES
typedef std::chrono::time_point<std::chrono::system_clock> TimePoint;


// CLASSES
class PerfCounter
{
public:
    PerfCounter(std::string name) : m_name(name) {}

    void startCount()
    {
        m_start = std::chrono::system_clock::now();
    }

    void stopCount()
    {
        m_stop = std::chrono::system_clock::now();
        long long count = std::chrono::duration_cast<std::chrono::milliseconds>(m_stop - m_start).count();
        m_samples.push_back(count);
    }

    void printAllSamples()
    {
        std::cout << "===================================" << std::endl << m_name << std::endl;
        int cnt = 0;
        for (auto sample : m_samples) {
            std::cout << "#" << cnt++ << ": " << sample << std::endl;
        }
    }

    void printStatistics()
    {
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wall"
        // std::vector<long long>::iterator itMax = std::max_element(m_samples.begin(), m_samples.end());
        long long max = 0;
        if (m_samples.size() != 0) {
            max = *(std::max_element(m_samples.begin(), m_samples.end()));
        }
        // long long max = *itMax;
        #pragma GCC diagnostic pop

        long long sum = std::accumulate(m_samples.begin(), m_samples.end(), 0);
        double mean = static_cast<double>(sum) / m_samples.size();

        double variance = 0, stdDeviation = 0;
        for (auto sample : m_samples) {
            variance += std::pow ((sample - mean), 2);
        }
        stdDeviation = std::sqrt(variance / m_samples.size());

        std::cout << "===================================" << std::endl << m_name << std::endl;
        std::cout << "mean: " << mean << " max: " << max << std::endl;
        std::cout << "95%:  " << mean + (2 * stdDeviation) << std::endl;
    }

private:
    std::string             m_name;
    std::vector<long long>  m_samples;
    TimePoint               m_start;
    TimePoint               m_stop;
};

#endif // PERFCOUNTER_H
