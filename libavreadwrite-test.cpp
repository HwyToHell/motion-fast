#include "libavreadwrite.h"

#include "motion-detector.h"
#include "safequeque.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <numeric> // accumulate
#include <thread>

// TYPES
typedef std::chrono::time_point<std::chrono::system_clock> TimePoint;


// CLASSES
struct State
{
    std::atomic_bool isMotion;
    std::atomic_bool isVideoSourceRunning;
    std::atomic_bool terminate;
};

class PacketBuffer
{
public:
    PacketBuffer(size_t bufSize) : m_size(bufSize) {}

    ~PacketBuffer()
    {
        while (m_buffer.size() > 0) {
            av_packet_free(&m_buffer.front());
            m_buffer.pop_front();
        }
    }

    void push_back(AVPacket *elem)
    {
        m_buffer.push_back(elem);
        if (m_buffer.size() > m_size) {
            av_packet_free(&m_buffer.front());
            m_buffer.pop_front();
        }
    }

    AVPacket * front()
    {
        if (m_buffer.size() == 0) {
            return nullptr;
        } else {
            return m_buffer.front();
        }
    }

    void pop_front()
    {
        if (m_buffer.size() > 0) {
            m_buffer.pop_front();
        }
    }

    size_t size()
    {
        return m_buffer.size();
    }
private:
    std::deque<AVPacket*> m_buffer;
    size_t m_size;

};


// FUNCTIONS
void statistics(std::vector<long long> samples, std::string name);

// TODO thread func -> decode, bgrsub, notify
int detectMotion(LibavReader reader, SafeQueue<AVPacket*>& packets, State& state)
{
    AVPacket* packet = nullptr;
    cv::Mat frame;
    MotionDetector detector;
    detector.bgrSubThreshold(50);       // foreground / background gray difference
    detector.minMotionDuration(10);     // consecutive frames
    detector.minMotionIntensity(1000);  // pixels

    cv::namedWindow("frame", cv::WINDOW_NORMAL);
    cv::resizeWindow("frame", cv::Size(950, 500) );
    cv::moveWindow("frame", 0, 0);

    cv::namedWindow("motion_mask", cv::WINDOW_NORMAL);
    cv::resizeWindow("motion_mask", cv::Size(950, 500) );
    cv::moveWindow("motion_mask", 970, 0);

    while (!state.terminate) {
        int cntDecoded = 0;
        while (packets.pop(packet)) {
            // decode packet
            if (!reader.decodePacket(packet)) {
                std::cout << "Failed to decode packet for motion detection" << std::endl;
            }
            av_packet_unref(packet);
            // count decoded packets for debugging purposes
            ++cntDecoded;
        }
        // retrieve frame
        if (!reader.retrieveFrame(frame)) {
            std::cout << "Failed to retrieve frame for motion detection" << std::endl;
        }
        // detect motion
        state.isMotion = detector.isContinuousMotion(frame);

        cv::imshow("frame", detector.motionMask());
        cv::imshow("motion_mask", detector.motionMask());
        std::cout << "decoded: " << cntDecoded << ", motion: " << state.isMotion << std::endl;
    }

    cv::destroyAllWindows();

    return 0;
}


int main(int argc, const char *argv[])
{
    if (argc < 2) {
        std::cout << "usage: libavreader videofile.mp4" << std::endl;
        return -1;
    }


    // performance measurement
    TimePoint readStart, readEnd;
    TimePoint decodeStart, decodeEnd;
    TimePoint writeStart, writeEnd;
    std::vector<long long> readTimes;
    std::vector<long long> decodeTimes;
    std::vector<long long> writeTimes;


    LibavReader reader;
    reader.init();
    PacketBuffer buffer(250);


    int ret = reader.open(argv[1]);
    if (ret < 0) {
        std::cout << "Error opening file: " << argv[1] << std::endl;
        return -1;
    }

    AVRational timeBase = reader.timeBase();
    if (timeBase.den == 0) {
        std::cout << "Failed to get time base: " << std::endl;
        return -1;
    }
    AVRational fps = reader.frameRate();
    if (fps.den == 0) {
        std::cout << "Failed to get fps: " << std::endl;
        return -1;
    }

    // motion detection thread
    State appState;
    appState.isMotion = false;
    appState.terminate = false;
    SafeQueue<AVPacket*> decodeQueue;
    //std::thread threadMotion(detectMotion, std::ref(reader), std::ref(decodeQueue), std::ref(appState));

    cv::Mat img;
    bool succ = true;
    std::cout << "press <ESC> to exit video processing" << std::endl;
    while (succ) {
        // measurement
        readStart = std::chrono::system_clock::now();
        if (!(succ = reader.readVideoPacket())) break;
        AVPacket* packet = reader.cloneVideoPacket();
        if(!packet) break;
        buffer.push_back(packet);
        decodeQueue.push(std::move(packet));

        // mesurement
        readEnd = std::chrono::system_clock::now();
        long long readDuration = std::chrono::duration_cast<std::chrono::microseconds>(readEnd - readStart).count();
        readTimes.push_back(readDuration);

        // measurement
        decodeStart = std::chrono::system_clock::now();
        if (!(succ = reader.decodePacket(packet))) break;
        if (!(succ = reader.retrieveFrame(img))) break;
        decodeEnd = std::chrono::system_clock::now();
        // measurement
        long long decodeDuration = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEnd - decodeStart).count();
        decodeTimes.push_back(decodeDuration);
        //cv::imshow("video frame", img);
        //if (cv::waitKey(10) == 27) break;

    }
    std::cout << "finished reading" << std::endl;

    LibavWriter writer;
    writer.open("write.mp4", reader.videoCodecParams());
    writer.frameRate(fps);
    writer.timeBase(timeBase);

    int cnt = 0;
    bool foundKeyFrame = false;
    while (buffer.size() > 0) {
        // measurement
        writeStart = std::chrono::system_clock::now();
        AVPacket* packet = buffer.front();
        //std::cout << "packet " << cnt++ << ", address: " << static_cast<void*>(packet) << ", size: " << packet->size << std::endl;
        std::cout << "packet " << cnt++ << ", size: " << packet->size << ", flags: " << packet->flags << std::endl;
        if (foundKeyFrame) {
            if (!(succ = writer.writeVideoPacket(packet))) {
                av_packet_free(&packet);
                buffer.pop_front();
                break;
            }
        } else {
            // start with key frame
            if (packet->flags & AV_PKT_FLAG_KEY) {
                foundKeyFrame= true;
                std::cout << "found keyframe" << std::endl;
                if (!(succ = writer.writeVideoPacket(packet))) {
                    av_packet_free(&packet);
                    buffer.pop_front();
                    break;
                }
            }
        }
        av_packet_free(&packet);
        buffer.pop_front();
        writeEnd = std::chrono::system_clock::now();
        long long writeDuration = std::chrono::duration_cast<std::chrono::microseconds>(writeEnd - writeStart).count();
        writeTimes.push_back(writeDuration);
    }
    std::cout << "finished writing buffer" << std::endl;
    statistics(readTimes, "read_packet in us");
    statistics(decodeTimes, "decode_packet in ms");
    statistics(writeTimes, "write_packet in us");

    appState.terminate = true;

    // TODO join thread
    std::cout << "wait for motion thread to join" << std::endl;
    threadMotion.join();
    std::cout << "motion thread joined" << std::endl;

    return 0;
}
