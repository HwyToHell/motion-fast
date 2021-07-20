#include "libavreadwrite.h"
#include "motion-detector.h"
#include "perfcounter.h"
#include "safebuffer.h"
#include "time-stamp.h"

// color cursor and getkey
#include "../cpp/inc/rlutil.h"

// TODO delete
extern "C" {
#include "ffmpeg/buffer_internal.h"
}


// std
#include <atomic>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

#ifdef DEBUG_BUILD
    #define DEBUG(x) do { std::cout << x << std::endl; } while (0)
#else
    #define DEBUG(x) do {} while (0)
#endif

#define DEBUGMSG(x) do { std::cout << x << std::endl; } while (0)



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
    std::thread             threadMotionDetection;
    std::thread             threadWritePackets;
    Motion                  motion;
    VideoStream             streamInfo;
    long long               errorCount;
    TimePoint               timeLastError;
    std::condition_variable resetDoneCnd;
    std::mutex              resetDoneMtx;
    bool                    reset;
    bool                    resetDone;
    bool                    terminate;
    char                    avoidPaddingWarning1[5];
};

enum class WriteState
{
    open,
    write,
    close
};


// FUNCTIONS





// motion detection thread func -> decode, bgrsub, notify
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
    detector.minMotionDuration(20);     // consecutive frames
    detector.minMotionIntensity(1000);  // pixels

    /*
    cv::namedWindow("frame", cv::WINDOW_NORMAL);
    cv::resizeWindow("frame", cv::Size(950, 500) );
    cv::moveWindow("frame", 0, 0);

    cv::namedWindow("motion_mask", cv::WINDOW_NORMAL);
    cv::resizeWindow("motion_mask", cv::Size(950, 500) );
    cv::moveWindow("motion_mask", 970, 0);
    */

    while (!appState.terminate) {
        // decode queued packets, if new packets are available
        packetQueue.waitForNewPacket();
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", new packet received");
        if (appState.terminate) break;
        int cntDecoded = 0;
        bool badDecode = false;

        while (packetQueue.pop(packet)) {
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet " << cntDecoded << " popped, pts: " << packet->pts << ", size: " << packet->size);
            if (appState.terminate) break;
            decode.startCount();
            if ((badDecode = !decoder.decodePacket(packet))) {
                std::cout << "Failed to decode packet for motion detection" << std::endl;
            }
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet decoded, queueSize: " << packetQueue.size());
            decode.stopCount();

            av_packet_unref(packet);
            // count decoded packets for debugging purposes
            ++cntDecoded;
        }

        // skip motion detection for partly decoded frames
        if (badDecode) continue;

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
                std::cout << getTimeStampMs() << " START MOTION ---------" << std::endl;
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
                std::cout<< getTimeStampMs() << " STOP MOTION ----------" << std::endl;
                appState.motion.stop = true;
            }
        }
        motion.stopCount();


        /*
        cv::imshow("frame", frame);
        cv::imshow("motion_mask", detector.motionMask());
        if (cv::waitKey(10) == 27)
            break;
        */

        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", motion detection finished");

        // detect motion dummy
        // std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", motion detection finished");

    }

    av_packet_free(&packet);
    // cv::destroyAllWindows();
    decoder.close();


    /*
    motion.printStatistics();
    detector.m_perfPre.printStatistics();
    detector.m_perfApply.printStatistics();
    detector.m_perfPost.printStatistics();
    decode.printStatistics();
    //decode.printAllSamples();
    */
    return 0;
}


void printAVErrorCodes()
{
    std::map<std::string, int> errorCodes;
    errorCodes.insert({"AVERROR_EOF: ", AVERROR_EOF});
    errorCodes.insert({"AVERROR_INVALIDDATA: ", AVERROR_INVALIDDATA});
    errorCodes.insert({"ENETUNREACH: ", ENETUNREACH});  // errno.h
    errorCodes.insert({"ETIMEDOUT: ", ETIMEDOUT});      // errno.h
    errorCodes.insert({"AVERROR(ENETUNREACH): ", AVERROR(ENETUNREACH)});
    errorCodes.insert({"AVERROR(ETIMEDOUT): ", AVERROR(ETIMEDOUT)});
    for (auto [key, value] : errorCodes) {
        std::cout << key << value << std::endl;
    }
}


bool processVideoStream(LibavReader& reader, PacketSafeQueue& decodeQueue, PacketSafeCircularBuffer& preCaptureBuffer, State& appState)
{
    bool succ = true;
    AVPacket* packetDecoder = nullptr;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << "Hit <ESC> to terminate video processing" << std::endl;

    //reader.playStream();
    int packetsRead = 0;
    while (succ) {

        if (!(succ = reader.readVideoPacket(packetDecoder))) break;

        //std::cout << "read packet " << cnt++ << ", pts: " << packet->pts << ", size: " << packet->size << std::endl;
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet read, pts: " << packetDecoder->pts );

        // clone packet for pre-capture buffer
        AVPacket* packetPreCaptureBuffer = av_packet_clone(packetDecoder);
        if(!packetPreCaptureBuffer) {
            std::cout << "nullptr packet motion buffer -> break" << std::endl;
            break;
        }

        decodeQueue.push(packetDecoder);
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet pushed");

        preCaptureBuffer.push(packetPreCaptureBuffer);

        // non-blocking getch
        if (rlutil::nb_getch() == 27) {
            std::cout << std::endl << "<ESC> pressed, terminating video processing" << std::endl;
            appState.terminate = true;
            return false;
        }
        ++packetsRead;
        if (!(packetsRead % 1500)) {
            std::cout << getTimeStampMs() << " packets read: " << packetsRead << std::endl;
        }
    }
    std::cout << "Video processing discontinued" << std::endl;
    return true;
}


void resetWriter(State& appState, WriteState& writeState, LibavWriter& writer)
{
    if (writer.isOpen()) {
        writer.close();
    }
    appState.motion.writeInProgress = false;
    writeState = WriteState::open;
    appState.resetDone = true;
    appState.reset = false;
    appState.resetDoneCnd.notify_one();
}


long long secondsWithoutError(State& appState)
{
    auto durationWithoutError = std::chrono::system_clock::now() - appState.timeLastError;
    appState.timeLastError = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(durationWithoutError).count();
}


void terminateThreads(PacketSafeQueue& packetQueue, PacketSafeCircularBuffer& buffer, State& appState)
{
    rlutil::setColor(rlutil::RED);
    std::cout << "Terminate application"  << std::endl;
    rlutil::resetColor();

    appState.terminate = true;

    std::cout << "Waiting for worker threads to join" << std::endl;
    packetQueue.terminate();
    appState.threadMotionDetection.join();
    std::cout << "Motion detection thread joined" << std::endl;

    appState.motion.startCnd.notify_one();
    buffer.terminate();
    appState.threadWritePackets.join();
    std::cout << "Write packets thread joined" << std::endl;
}


void waitForMotion(State& appState)
{
    // wait for motion condition
    std::unique_lock<std::mutex> lock(appState.motion.startMtx);
    appState.motion.startCnd.wait(lock, [&]{return (appState.motion.start || appState.reset || appState.terminate);});
    appState.motion.start = false;
    appState.motion.writeInProgress = true;
}


// demux from buffer thread func -> write video packets, if motion present
int writeMotionPackets(PacketSafeCircularBuffer& buffer, State& appState)
{
    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", thread write packets started");
    WriteState writeState = WriteState::open;

    LibavWriter writer;
    writer.init();
    int postCapture = appState.motion.postCapture;

    while (!appState.terminate) {
        switch (writeState) {

        case WriteState::open:
        {
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", waitForMotion");
            waitForMotion(appState);
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", waitForMotion triggered, bufSize: " << buffer.size());
            if (appState.terminate) {
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", terminate");
                break;
            }
            if (appState.reset) {
                resetWriter(appState, writeState, writer);
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", reset writer");
                break;
            }

            std::string fileName = getTimeStamp(TimeResolution::sec_NoBlank) + ".mp4";
            int ret = writer.open(fileName, appState.streamInfo);
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
                writeState = WriteState::close;
            }

            // drain pre-capture buffer
            while(buffer.pop(packet)) {
                writer.writeVideoPacket(packet);
            }

            // reset -> close writer, stay in open state
            if (appState.reset || appState.terminate) {
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", open: reset -> close writer");
                resetWriter(appState, writeState, writer);
            } else {
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", open finished -> write");
                writeState = WriteState::write;
            }

            break;
        }

        case WriteState::write:
        {
            buffer.waitForNewPacket();
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", write: waitForNewPacket triggered, bufSize: " << buffer.size());
            AVPacket* packet;
            while(buffer.pop(packet)) {
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet popped (pts: " << packet->pts << ")");
                writer.writeVideoPacket(packet);
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet written");

                // reset -> open
                if (appState.terminate || appState.reset) {
                    resetWriter(appState, writeState, writer);
                    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", write: reset -> open");
                    break;
                // no motion -> writePostCapture (only if no reset)
                } else if (appState.motion.stop) {
                    postCapture = appState.motion.postCapture;
                    writeState = WriteState::close;
                    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", write: motion.stop -> close");
                    break;
                }
            }
            break;
        }
        case WriteState::close:
        {
            buffer.waitForNewPacket();
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", close: waitForNewPacket triggered, bufSize: " << buffer.size());
            AVPacket* packet;
            while(buffer.pop(packet)) {
                writer.writeVideoPacket(packet);
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", post-capture packet written, remaining: " << postCapture);

                // reset -> open
                if ((--postCapture == 0) || appState.terminate || appState.reset) {
                    resetWriter(appState, writeState, writer);
                    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", close: post-capture finished or reset -> open");
                    break;
                }
            }
            break;
        }

        } // end switch
    } // end while (!terminate)
    return 0;
}



int main(int argc, const char *argv[])
{
    if (argc < 2) {
        std::cout << "usage: libavreader videofile.mp4" << std::endl;
        return -1;
    }

    /*
    printAVErrorCodes();
    return 0;

    rlutil::setColor(rlutil::RED);
    std::cout << "Connection timed out, trying to re-connect in "
              << std::setw(3) << 100 << " sec" << std::endl; //"\r";
    rlutil::resetColor();

    std::cout << "Connection timed out, trying to re-connect in "
              << std::setw(3) << 1 << " sec" << std::endl; //"\r";

    //std::cout.flush();

    //testRefAVBuffer(argv[1]);
    // testNewDecoder(argv[1]);
    // return 0;
    */

    avformat_network_init();
    LibavReader reader;
    reader.init();

    // TODO check input stream
    int ret = reader.open(argv[1]);
    if (ret < 0) {
        std::cout << "Error opening input: " << argv[1] << std::endl;
        return -1;
    } else {
        std::cout << "Video input opened successfully: "  << argv[1] << std::endl;
    }

    // prepare for motion detection thread
    // TODO refactor State as class with initializing valid state
    State appState;
    appState.motion.start = false; // needs lock, if detection thread already running
    appState.motion.stop = true;
    appState.motion.writeInProgress = false;
    appState.motion.postCapture = 25;
    appState.reset = false;
    appState.resetDone = false;
    appState.terminate = false;
    if (!reader.getVideoStreamInfo(appState.streamInfo)) {
        std::cout << "Failed to get video stream info" << std::endl;
        return -1;
    }
    appState.errorCount = 0;
    appState.timeLastError = std::chrono::system_clock::now();

    PacketSafeQueue decodeQueue;
    appState.threadMotionDetection = std::thread(detectMotionCnd, std::ref(decodeQueue), std::ref(appState));
    // std::thread threadMotionDetection(detectMotionCnd, std::ref(decodeQueue), std::ref(appState));

    PacketSafeCircularBuffer preCaptureBuffer(100);
    appState.threadWritePackets = std::thread(writeMotionPackets, std::ref(preCaptureBuffer), std::ref(appState));
    // std::thread threadWritePackets(writeMotionPackets, std::ref(preCaptureBuffer), std::ref(appState));

    // reader.openPaused()
     while (!appState.terminate) {
        if (!reader.isOpen()) {
            ret = -1;
            while (ret) {
                ret = reader.open(argv[1]);

                // keep trying to connect
                if (ret == AVERROR(ETIMEDOUT) || AVERROR(ENETUNREACH)) {
                    int remainingSeconds = 15;
                    while (remainingSeconds) {
                        std::cout << "Connection timed out, trying to re-connect in"
                                  << std::setw(3) << --remainingSeconds << " sec" << "\r";
                        std::cout.flush();
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    std::cout << "Re-connecting ...                                      " << std::endl;
                    continue;
                // terminate application for any other error
                } else if (ret < 0) {
                    std::cout << "Failed to open input: "  << argv[1] << std::endl;
                    terminateThreads(decodeQueue, preCaptureBuffer, appState);
                    return -1;
                }
            }
            std::cout << "Video input opened successfully: "  << argv[1] << std::endl;
            if (!reader.getVideoStreamInfo(appState.streamInfo)) {
                std::cout << "Failed to get video stream info" << std::endl;
                terminateThreads(decodeQueue, preCaptureBuffer, appState);
                return -1;
            }
        }
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", reader open");
        processVideoStream(reader, decodeQueue, preCaptureBuffer, appState);   
        if (appState.terminate) break;

        // reset state of threadMotionDetection and threadWritePackets
        appState.reset = true;
        decodeQueue.reset();
        preCaptureBuffer.reset(); // notifies newPacket
        appState.motion.startCnd.notify_one(); // notifies waitForMotion in order to get resetDone notification

        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", waiting for resetDone");
        std::unique_lock<std::mutex> lockReset(appState.resetDoneMtx);
        appState.resetDoneCnd.wait(lockReset, [&]{return(appState.resetDone);});
        appState.resetDone = false;
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", resetDone triggered");

        reader.close();
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", reader closed");

        // TODO inc failcount, measure fail time
        std::cout << "Reading errors: " << ++appState.errorCount << ", seconds since last error: "
                  << secondsWithoutError(appState) << std::endl;
    }

    terminateThreads(decodeQueue, preCaptureBuffer, appState);
    return 0;
}
