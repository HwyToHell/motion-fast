#include "libavreadwrite.h"
extern "C" {
#include "ffmpeg/buffer_internal.h"
}

#include "motion-detector.h"
#include "perfcounter.h"
#include "safebuffer.h"
#include "safequeque.h" // TODO delete
#include "time-stamp.h"

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#ifdef DEBUG_BUILD
    #define DEBUG(x) do { std::cout << x << std::endl; } while (0)
#else
    #define DEBUG(x) do {} while (0)
#endif



// CLASSES
struct Motion
{
    std::condition_variable startCnd;
    std::mutex              startMtx;
    int                     postCapture;
    bool                    start;
    std::atomic_bool        stop;
    bool                    writeInProgress;
    char                    avoidPaddingWarning1[1];
};

struct State
{
    Motion                  motion;
    VideoStream             streamInfo;
    bool                    terminate;
    char                    avoidPaddingWarning1[7];
};


// FUNCTIONS


// test new decoder 2021-07-01
int testNewDecoder(const char * fileName)
{
    LibavReader reader;
    reader.init();
    PacketSafeCircularBuffer buffer(100);

    int ret = reader.open(fileName);
    if (ret < 0) {
        std::cout << "Error opening file: " << fileName << std::endl;
        return -1;
    }

    // states for motion detection thread
    State appState;
    appState.motion.start = false;
    appState.terminate = false;
    if (!reader.getVideoStreamInfo(appState.streamInfo)) {
        std::cout << "Failed to get video stream info" << std::endl;
        return -1;
    }

    LibavDecoder decoder;
    decoder.open(appState.streamInfo.videoCodecParameters);

    PacketSafeQueue decodeQueue;

    bool succ = true;
    int cnt = 0;

    while (succ) {
    //for (cnt = 0; cnt<5; ++cnt) {
        // read packet for decoder
        AVPacket* packetDecoder = nullptr;
        if (!(succ = reader.readVideoPacket(packetDecoder))) break;
        decodeQueue.push(packetDecoder);
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet decoder" << cnt << " read" );

        // clone packet for pre-capture buffer
        AVPacket* packetPreCaptureBuffer = av_packet_clone(packetDecoder);
        if(!packetPreCaptureBuffer) {
            std::cout << "nullptr packet motion buffer -> break" << std::endl;
            break;
        }
        buffer.push(packetPreCaptureBuffer);

        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", queue size: " << decodeQueue.size());
        ++cnt;
    }
    std::cout << "finished reading " << cnt << " packets" << std::endl;
    std::cout << "motion buffer size: " << buffer.size() << std::endl;
    std::cout << "decode queue size:  " << decodeQueue.size() << std::endl;

    // prepare opencv windows
    cv::namedWindow("frame", cv::WINDOW_NORMAL);
    cv::resizeWindow("frame", cv::Size(950, 500) );
    cv::moveWindow("frame", 0, 0);
    cv::Mat frame;

    // pop decode queue
    // size_t nSize = decodeQueue.size();
    succ = true;
    cnt = 0;
    std::cout << "ESC to break" << std::endl;
    while (succ) {
    //for (size_t n = 0; n < nSize; ++n) {
        AVPacket* packetDecode = nullptr;
        if (!decodeQueue.pop(packetDecode)) {
            std::cout << "Failed to pop decode packet" << std::endl;
            break;
        } else {
            // decode
            if (!decoder.decodePacket(packetDecode)) {
                std::cout << "Failed to decode packet for motion detection" << std::endl;
            }
            av_packet_free(&packetDecode);

            // get opencv image
            if (!decoder.retrieveFrame(frame)) {
                std::cout << "Failed to retrieve frame for motion detection" << std::endl;
            }
            cv::imshow("frame", frame);
            ++cnt;
            if (cv::waitKey(20) == 27)
                break;
        }
    }
    cv::destroyAllWindows();

    // pop pre-capture buffer
    cnt = 0;
    AVPacket* packetBuffer = nullptr;
    while (buffer.pop(packetBuffer)) {
        av_packet_free(&packetBuffer);
        ++cnt;
    }

    return 0;
}




// test av_packet_ref und av_packet_unref with old LiabavReader 2021-06-28
int testRefAVBuffer(const char * fileName)
{
    LibavReader reader;
    reader.init();
    PacketSafeCircularBuffer buffer(10);


    int ret = reader.open(fileName);
    if (ret < 0) {
        std::cout << "Error opening file: " << fileName << std::endl;
        return -1;
    }

    // motion detection thread
    State appState;
    appState.motion.start = false;
    appState.terminate = false;
    if (!reader.getVideoStreamInfo(appState.streamInfo)) {
        std::cout << "Failed to get video stream info" << std::endl;
        return -1;
    }

    PacketSafeQueue decodeQueue;

    cv::Mat img;
    bool succ = true;
    std::cout << "press <ESC> to exit video processing" << std::endl;
    int cnt = 0;
    // while (succ) {
    for (int i = 0; i<5; ++i) {
        // read packet for decoder
        AVPacket* packetDecoder = nullptr;
        if (!(succ = reader.readVideoPacket(packetDecoder))) break;
        decodeQueue.push(packetDecoder);
        std::cout   << "packet decoder " << i << std::endl
                    << "addr: " << static_cast<void*>(packetDecoder)
                    << ", size: " << packetDecoder->size
                    << ", refCnt: " << packetDecoder->buf->buffer->refcount << std::endl;
        // getting the buffer ref_count via pointer requires a workaround: buffer_internal.h must be included
        //   and atomic types changed to non-atomic ones in order to compile with c++
        //   better solution: use av_buffer_get_ref_count() - see example below

        //std::cout << "read packet " << cnt++ << ", pts: " << packetDecoder->pts << ", size: " << packetDecoder->size << std::endl;
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet decoder" << cnt << " read" );

        // clone packet for pre-capture buffer
        AVPacket* packetPreCaptureBuffer = av_packet_clone(packetDecoder);
        if(!packetPreCaptureBuffer) {
            std::cout << "nullptr packet motion buffer -> break" << std::endl;
            break;
        }
        std::cout   << "packet motion buffer " << i << std::endl
                    << "addr: " << static_cast<void*>(packetPreCaptureBuffer)
                    << ", size: " << packetPreCaptureBuffer->size
                    << ", refCntFn: " << av_buffer_get_ref_count(packetPreCaptureBuffer->buf) << std::endl;


        buffer.push(packetPreCaptureBuffer);

        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", queue size: " << decodeQueue.size());

        //cv::imshow("video frame", img);
        //if (cv::waitKey(10) == 27) break;
        ++cnt;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));

    }
    // mit readVideoPacket() nicht erforderlich
    // AVPacket* pkt = reader.getVideoPacket();
    // av_packet_unref(pkt);

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
    }


    // pop pre-capture buffer
    cnt = 0;
    AVPacket* packetBuffer = nullptr;
    while (buffer.pop(packetBuffer)) {
        std::cout   << "packet motion buffer " << cnt << std::endl
                    << "addr: " << static_cast<void*>(packetBuffer)
                    << ", size: " << packetBuffer->size
                    << ", refCnt: " << packetBuffer->buf->buffer->refcount << std::endl;

        //av_packet_unref(packetBuffer);
        av_packet_free(&packetBuffer);
        std::cout   << "freed addr: " << static_cast<void*>(packetBuffer) << std::endl;
        ++cnt;
    }

    return 0;
}



// test motion detection thread func -> decode, bgrsub, notify 2021-07-04
int detectMotionCnd(PacketSafeQueue& packetQueue, State& appState)
{
    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", thread motion started");
    PerfCounter decode("decoding");
    PerfCounter motion("motion detection");

    LibavDecoder decoder;
    int ret = decoder.open(appState.streamInfo.videoCodecParameters);
    if (ret < 0){
        avErrMsg("Failed to open codec", ret);
        return ret;
    }
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

    while (!appState.terminate) {
        // decode queued packets, if new packets are available
        packetQueue.waitForNewPacket();
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", new packet received");
        if (appState.terminate) break;
        int cntDecoded = 0;

        while (packetQueue.pop(packet)) {
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet "
                  << cntDecoded << " popped, pts: " << packet->pts << ", size: " << packet->size);
            if (appState.terminate) break;
            decode.startCount();
            if (!decoder.decodePacket(packet)) {
                std::cout << "Failed to decode packet for motion detection" << std::endl;
            }
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet decoded, queueSize: " << packetQueue.size());
            decode.stopCount();

            av_packet_unref(packet);
            // count decoded packets for debugging purposes
            ++cntDecoded;
        }

        // retrieve last frame
        if (!decoder.retrieveFrame(frame)) {
            std::cout << "Failed to retrieve frame for motion detection" << std::endl;
        }
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", frame retrieved, time "  << decoder.frameTime(appState.streamInfo.timeBase) << " sec");



        // detect motion
        motion.startCount();
        bool isMotion = detector.isContinuousMotion(frame);
        if (isMotion) {
            if (!appState.motion.writeInProgress) {
                std::cout << "START MOTION" << std::endl;
                {
                    std::lock_guard<std::mutex> lock(appState.motion.startMtx);
                    appState.motion.start = true;
                    appState.motion.stop = false;
                    // appState.motion.writeInProgress = true; // move to write motion packets thread
                }
                appState.motion.startCnd.notify_one();
            }
        } else {
            if (!appState.motion.stop) {
                std::cout << "STOP MOTION" << std::endl;
                appState.motion.stop = true;
            }
        }
        motion.stopCount();


        cv::imshow("frame", frame);
        cv::imshow("motion_mask", detector.motionMask());
        if (cv::waitKey(10) == 27)
            break;
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", motion detection finished");


        // detect motion dummy
        // std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", motion detection finished");

    }

    av_packet_free(&packet);
    cv::destroyAllWindows();
    decoder.close();


    motion.printStatistics();
    detector.m_perfPre.printStatistics();
    detector.m_perfApply.printStatistics();
    detector.m_perfPost.printStatistics();
    decode.printStatistics();
    //decode.printAllSamples();
    return 0;
}



void waitForMotion(State& appState)
{
    // wait for motion condition
    std::unique_lock<std::mutex> lock(appState.motion.startMtx);
    appState.motion.startCnd.wait(lock, [&]{return (appState.motion.start || appState.terminate);});
    appState.motion.start = false;
    appState.motion.writeInProgress = true;
}


// test muxing from buffer thread func 2021-07-06
int writeMotionPackets(PacketSafeCircularBuffer& buffer, State& appState)
{
    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", thread write packets started");
    enum class State
    {
        open,
        write,
        close
    } writeState = State::open;

    LibavWriter writer;
    writer.init();
    int postCapture = appState.motion.postCapture;

    while (!appState.terminate) {
        switch (writeState) {

        case State::open:
        {
            waitForMotion(appState);
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", waitForMotion triggered, bufSize: " << buffer.size());
            if (appState.terminate) {
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", terminate");
                break;
            }

            // open writer // TODO - use time stamp for file name
            int ret = writer.open("out.mp4", appState.streamInfo);
            if (ret < 0) {
                avErrMsg("Failed to open output file", ret);
                appState.terminate = true;
                break;
            }

            // find keyframe and start writing
            AVPacket* packet;
            if (buffer.popToNextKeyFrame(packet)) {
                writer.writeVideoPacket(packet);
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", key frame written");
            } else {
                std::cout << "no key frame found -> close output file" << std::endl;
                writeState = State::close;
            }

            // drain pre-capture buffer
            while(buffer.pop(packet)) {
                writer.writeVideoPacket(packet);
            }

            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", open finished -> write");
            writeState = State::write;
            break;
        }

        case State::write:
        {
            buffer.waitForNewPacket();
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", write: waitForNewPacket triggered, bufSize: " << buffer.size());
            AVPacket* packet;
            while(buffer.pop(packet)) {
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet popped (pts: " << packet->pts << ")");
                writer.writeVideoPacket(packet);
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet written");

                // no motion -> writePostCapture
                if (appState.motion.stop) {
                    postCapture = appState.motion.postCapture;
                    writeState = State::close;
                    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", motion.stop -> close");
                    break;
                }

                if (appState.terminate) {
                    writer.close();
                    appState.motion.writeInProgress = false;
                    writeState = State::open;
                    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", motion.terminate -> open");
                    break;
                }
            }
            break;
        }
        case State::close:
        {
            buffer.waitForNewPacket();
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", close: waitForNewPacket triggered, bufSize: " << buffer.size());
            AVPacket* packet;
            while(buffer.pop(packet)) {
                writer.writeVideoPacket(packet);
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", post-capture packet written, remaining: " << postCapture);

                if ((--postCapture == 0) || appState.terminate) {
                    writer.close();
                    appState.motion.writeInProgress = false;
                    writeState = State::open;
                    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", post-capture finished -> open");
                    break;
                }
            }
            break;
        }

        } // end switch
    } // end while (!terminate)
    return 0;
}





int main_test_libav(int argc, const char *argv[])
{
    if (argc < 2) {
        std::cout << "usage: libavreader videofile.mp4" << std::endl;
        return -1;
    }

    avformat_network_init();

    //testRefAVBuffer(argv[1]);
    // testNewDecoder(argv[1]);
    // return 0;

    // performance measurement
    TimePoint readStart, readEnd;
    TimePoint decodeStart, decodeEnd;
    // TimePoint writeStart, writeEnd;
    std::vector<long long> readTimes;
    std::vector<long long> decodeTimes;
    std::vector<long long> writeTimes;

    LibavReader reader;
    reader.init();

    int ret = reader.open(argv[1]);
    if (ret < 0) {
        std::cout << "Error opening file: " << argv[1] << std::endl;
        return -1;
    }

    // prepare for motion detection thread
    // TODO refactor State as class with initializing valid state
    State appState;
    appState.motion.start = false; // needs lock, if detection thread already running
    appState.motion.stop = true;
    appState.motion.writeInProgress = false;
    appState.motion.postCapture = 25;
    appState.terminate = false;
    if (!reader.getVideoStreamInfo(appState.streamInfo)) {
        std::cout << "Failed to get video stream info" << std::endl;
        return -1;
    }

    PacketSafeQueue decodeQueue;
    std::thread threadMotionDetection(detectMotionCnd, std::ref(decodeQueue), std::ref(appState));

    PacketSafeCircularBuffer preCaptureBuffer(100);
    std::thread threadWritePackets(writeMotionPackets, std::ref(preCaptureBuffer), std::ref(appState));

    cv::Mat img;
    bool succ = true;
    int cnt = 0;
    AVPacket* packetDecoder = nullptr;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << "press <ESC> to exit video processing" << std::endl;

    //reader.playStream();

    while (succ) {
    //for (int i = 0; i < 30; ++i) {
        // timer_start
        readStart = std::chrono::system_clock::now();

        if (!(succ = reader.readVideoPacket(packetDecoder))) break;

        //std::cout << "read packet " << cnt++ << ", pts: " << packet->pts << ", size: " << packet->size << std::endl;
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet " << cnt << " read" );

        // clone packet for pre-capture buffer
        AVPacket* packetPreCaptureBuffer = av_packet_clone(packetDecoder);
        if(!packetPreCaptureBuffer) {
            std::cout << "nullptr packet motion buffer -> break" << std::endl;
            break;
        }

        decodeQueue.push(packetDecoder);
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet pushed");

        preCaptureBuffer.push(packetPreCaptureBuffer);

        // timer_end
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
        //std::this_thread::sleep_for(std::chrono::milliseconds(40));

    }
    std::cout << "finished reading" << std::endl;

    appState.terminate = true;

    // TODO join thread
    std::cout << "wait for worker threads to join" << std::endl;
    decodeQueue.terminate();
    threadMotionDetection.join();
    std::cout << "motion detection thread joined" << std::endl;

    appState.motion.startCnd.notify_one();
    preCaptureBuffer.terminate();
    threadWritePackets.join();
    std::cout << "write packets thread joined" << std::endl;

    return 0;
}
