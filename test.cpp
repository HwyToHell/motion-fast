#include "libavreadwrite.h"
extern "C" {
#include "ffmpeg/buffer_internal.h"
}

#include "motion-detector.h"
#include "safebuffer.h"
#include "safequeque.h" // TODO delete
#include "time-stamp.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <numeric>  // accumulate
#include <thread>

#ifdef DEBUG_BUILD
    #define DEBUG(x) do { std::cout << x << std::endl; } while (0)
#else
    #define DEBUG(x) do {} while (0)
#endif

// TYPES
typedef std::chrono::time_point<std::chrono::system_clock> TimePoint;


// CLASSES
struct State
{
    std::atomic_bool        isMotion;
    std::atomic_bool        isVideoSourceRunning;
    bool                    newPacket;
    char                    avoidPaddingWarning1[5];
    std::condition_variable newPacketCnd;
    std::mutex              newPacketMtx;
    std::atomic_bool        terminate;
    char                    avoidPaddingWarning2[7];
};

class PacketBuffer // TODO delete, use classes of safebuffer.h
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

// test av_packet_ref und av_packet_unref
int testRef(const char * fileName)
{
    LibavReader reader;
    reader.init();
    PacketBuffer buffer(10); // TODO PacketSafeCircularBuffer


    int ret = reader.open(fileName);
    if (ret < 0) {
        std::cout << "Error opening file: " << fileName << std::endl;
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
    SafeQueue<AVPacket*> decodeQueue;  // TODO PacketSafeQueue

    cv::Mat img;
    bool succ = true;
    std::cout << "press <ESC> to exit video processing" << std::endl;
    int cnt = 0;
    // while (succ) {
    for (int i = 0; i<5; ++i) {
        // measurement
        if (!(succ = reader.readVideoPacket())) break;

        AVPacket* packetDecoder = reader.cloneVideoPacket();
        if(!packetDecoder) {
            std::cout << "nullptr packet decoder -> break" << std::endl;
            break;
        }

        //std::cout << "read packet " << cnt++ << ", pts: " << packetDecoder->pts << ", size: " << packetDecoder->size << std::endl;
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet decoder" << cnt << " read" );

        AVPacket* packetMotionBuffer = reader.cloneVideoPacket();
        if(!packetMotionBuffer) {
            std::cout << "nullptr packet motion buffer -> break" << std::endl;
            break;
        }



        std::cout   << "packet decoder " << i << std::endl
                    << "addr: " << static_cast<void*>(packetDecoder)
                    << ", size: " << packetDecoder->size
                    << ", refCnt: " << packetDecoder->buf->buffer->refcount << std::endl;

        std::cout   << "packet motion buffer " << i << std::endl
                    << "addr: " << static_cast<void*>(packetMotionBuffer)
                    << ", size: " << packetMotionBuffer->size
                    << ", refCnt: " << packetMotionBuffer->buf->buffer->refcount << std::endl;

        buffer.push_back(std::move(packetMotionBuffer));
        decodeQueue.push(std::move(packetDecoder));
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", queue size: " << decodeQueue.size());
        appState.newPacket = true;
        appState.newPacketCnd.notify_one();
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", new packet "<< cnt << " wait notified");


        //cv::imshow("video frame", img);
        //if (cv::waitKey(10) == 27) break;
        ++cnt;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));

    }
    std::cout << "finished reading" << std::endl;

    std::cout << "motion buffer size: " << buffer.size() << std::endl;
    std::cout << "decode queue size:  " << decodeQueue.size() << std::endl;



    // pop decode queue
    size_t nSize = decodeQueue.size();
    for (size_t n = 0; n < nSize; ++n) {
        AVPacket* packetDecode = nullptr;
        if (!decodeQueue.pop(packetDecode)) {
            std::cout   << "Failed to pop decode packet" << std::endl;
            break;
        } else {
            std::cout   << "packet decoder " << n << std::endl
                        << "addr: " << static_cast<void*>(packetDecode)
                        << ", size: " << packetDecode->size
                        << ", refCnt: " << packetDecode->buf->buffer->refcount << std::endl;
        }

        //av_packet_unref(packetDecode);
        av_packet_free(&packetDecode);

        AVPacket* packetBuffer = buffer.front();

        std::cout   << "freed addr: " << static_cast<void*>(packetDecode) << std::endl;
    }


    // pop pre-capture buffer
    nSize = buffer.size();
    for (size_t n = 0; n < nSize; ++n) {
        AVPacket* packetBuffer = buffer.front();
        std::cout   << "packet motion buffer " << n << std::endl
                    << "addr: " << static_cast<void*>(packetBuffer)
                    << ", size: " << packetBuffer->size
                    << ", refCnt: " << packetBuffer->buf->buffer->refcount << std::endl;
        buffer.pop_front();

        //av_packet_unref(packetBuffer);
        av_packet_free(&packetBuffer);
        std::cout   << "freed addr: " << static_cast<void*>(packetBuffer) << std::endl;
    }

    return 0;
}



// TODO thread func -> decode, bgrsub, notify
int detectMotionCnd(LibavReader& reader, SafeQueue<AVPacket*>& packets, State& state)
{
    AVPacket* packet = nullptr;
    cv::Mat frame;

    std::cout << "terminate == false? " << (state.terminate == false) << std::endl;
    while (state.terminate == false) {
        {
            std::unique_lock<std::mutex> lock(state.newPacketMtx);
            state.newPacketCnd.wait(lock, [&]{return state.newPacket;});
            state.newPacket = false;
        }
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", new packet wait finished");

        // decode queued packets
        int cntDecoded = 0;
        while (packets.pop(packet)) {
            std::cout << "packet " << cntDecoded << " popped, pts: " << packet->pts << ", size: " << packet->size << std::endl;
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet popped");
            if (!reader.decodePacket(packet)) {
                std::cout << "Failed to decode packet for motion detection" << std::endl;
            }
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet decoded");

            av_packet_unref(packet);
            // count decoded packets for debugging purposes
            ++cntDecoded;
        }

        // retrieve last frame
        if (!reader.retrieveFrame(frame)) {
            std::cout << "Failed to retrieve frame for motion detection" << std::endl;
        }
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", frame retrieved");

        // detect motion dummy
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", motion detection finished");

    }
    return 0;
}


int detectMotion(LibavReader& reader, SafeQueue<AVPacket*>& packets, State& state)
{
    std::cout << "state.terminate: " << state.terminate << std::endl;
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

    //TODO Threadfunction: Condition Variable einführen, notify, wenn neues Paket gelesen

    //while (!state.terminate) {
   // for (int i = 0; i < 5; ++i) {

        int cntDecoded = 0;
        while (packets.pop(packet)) {
            std::cout << "pop packet " << cntDecoded << ", pts: " << packet->pts << ", size: " << packet->size << std::endl;
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

        cv::imshow("frame", frame);
        cv::imshow("motion_mask", detector.motionMask());
        std::cout << "ESC to continue" << std::endl;
        cv::waitKey(0);
        std::cout << "decoded: " << cntDecoded << ", motion: " << state.isMotion << std::endl;
    //}

    cv::destroyAllWindows();

    return 0;
}


int main(int argc, const char *argv[])
{
    if (argc < 2) {
        std::cout << "usage: libavreader videofile.mp4" << std::endl;
        return -1;
    }

    testRef(argv[1]);
    return 0;

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
    std::thread threadMotion(detectMotionCnd, std::ref(reader), std::ref(decodeQueue), std::ref(appState));

    cv::Mat img;
    bool succ = true;
    std::cout << "press <ESC> to exit video processing" << std::endl;
    int cnt = 0;
    while (succ) {
    //for (int i = 0; i<5; ++i) {
        // measurement
        readStart = std::chrono::system_clock::now();
        if (!(succ = reader.readVideoPacket())) break;
        AVPacket* packet = reader.cloneVideoPacket();

        //std::cout << "read packet " << cnt++ << ", pts: " << packet->pts << ", size: " << packet->size << std::endl;
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet " << cnt << " read" );

        if(!packet) {
            std::cout << "nullptr packet -> break" << std::endl;
            break;
        }
        //buffer.push_back(packet);
        decodeQueue.push(std::move(packet));
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", queue size: " << decodeQueue.size());
        appState.newPacket = true;
        appState.newPacketCnd.notify_one();
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", new packet "<< cnt << " wait notified");

        // mesurement
        readEnd = std::chrono::system_clock::now();
        long long readDuration = std::chrono::duration_cast<std::chrono::microseconds>(readEnd - readStart).count();
        readTimes.push_back(readDuration);

        // measurement
        decodeStart = std::chrono::system_clock::now();
        //if (!(succ = reader.decodePacket(packet))) break;
        //if (!(succ = reader.retrieveFrame(img))) break;
        decodeEnd = std::chrono::system_clock::now();
        // measurement
        long long decodeDuration = std::chrono::duration_cast<std::chrono::milliseconds>(decodeEnd - decodeStart).count();
        decodeTimes.push_back(decodeDuration);
        //cv::imshow("video frame", img);
        //if (cv::waitKey(10) == 27) break;
        ++cnt;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));

    }
    std::cout << "finished reading" << std::endl;

    //detectMotion(reader, decodeQueue, appState);

/*    LibavWriter writer;
    writer.open("write.mp4", reader.videoCodecParams());
    writer.frameRate(fps);
    writer.timeBase(timeBase);


    bool foundKeyFrame = false;
    while (buffer.size() > 0) {
        // measurement
        writeStart = std::chrono::system_clock::now();
        AVPacket* packet = buffer.front();
        //std::cout << "packet " << cnt++ << ", address: " << static_cast<void*>(packet) << ", size: " << packet->size << std::endl;
        //std::cout << "packet " << cnt++ << ", size: " << packet->size << ", flags: " << packet->flags << std::endl;
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
*/
    appState.terminate = true;
    appState.newPacket = true;
    appState.newPacketCnd.notify_one();

    // TODO join thread
    std::cout << "wait for motion thread to join" << std::endl;
    threadMotion.join();
    std::cout << "motion thread joined" << std::endl;

    return 0;
}
