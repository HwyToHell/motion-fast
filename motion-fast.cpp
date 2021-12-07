#include "avreadwrite.h"
#include "motion-detector.h"
#include "perfcounter.h"
#include "safebuffer.h"
#include "time-stamp.h"

// color cursor and getkey
#include "../cpp/inc/rlutil.h"

// opencv
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/filesystem.hpp>

// qt
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QRect>
#include <QSettings>

// std
#include <atomic>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

#include <csignal>
#include <unistd.h> // getpid

#ifdef DEBUG_BUILD
    #define DEBUG(x) do { std::cout << x << std::endl; } while (0)
#else
    #define DEBUG(x) do {} while (0)
#endif

#define DEBUGMSG(x) do { std::cout << x << std::endl; } while (0)


// GLOBALS
static volatile std::sig_atomic_t g_terminate = 0;

// CLASSES
struct MotionCondition
{
    std::condition_variable startCnd;
    std::mutex              startMtx;
    bool                    start;
    std::atomic_bool        stop;
    bool                    writeInProgress;
    char                    avoidPaddingWarning1[5];
};


struct Params
{
    Params();
    Params(const Params&) = default;
    ~Params();
    DetectorParams      detector;
    void                loadSettings();
    void                saveSettings();
};


struct State
{
    std::thread                 threadMotionDetection;
    std::thread                 threadWritePackets;
    DetectorParams              detector;
    MotionCondition             motion;
    std::vector<MotionDiagPic>  motionDiag;
    VideoStream                 streamInfo;
    long long                   errorCount;
    TimePoint                   timeLastError;
    std::condition_variable     resetDoneCnd;
    std::mutex                  resetDoneMtx;
    bool                        reset;
    bool                        resetDone;
    bool                        terminate;
    bool                        debug;
    char                        avoidPaddingWarning1[4];
};


enum class WriteState
{
    open,
    write,
    close
};



// MEMBER FUNCTIONS
// TODO move to separate class PersistentParams
Params::Params()
{
    loadSettings();
}

Params::~Params()
{
    saveSettings();
}

void Params::loadSettings()
{
    QSettings settings;

    settings.beginGroup("MotionDetector");
    detector.bgrSubThreshold = settings.value("bgrSubThreshold", 40).toDouble();
    detector.debug = settings.value("debug", false).toBool();
    detector.minMotionDuration = settings.value("minMotionDuration", 30).toInt();
    detector.minMotionIntensity = settings.value("minMotionIntensity", 80).toInt();
    QRect qRoi = settings.value("roi", QRect(0,0,0,0)).toRect();
    detector.postCapture = settings.value("postBuffer", 25).toInt();
    detector.roi = cv::Rect(qRoi.x(), qRoi.y(), qRoi.width(),qRoi.height());
    detector.scaleFrame = settings.value("scaleFrame", 0.25).toDouble();
    settings.endGroup();
}

void Params::saveSettings()
{
    QSettings settings;

    settings.beginGroup("MotionDetector");
    settings.setValue("bgrSubThreshold", detector.bgrSubThreshold);
    settings.setValue("debug", detector.debug);
    settings.setValue("minMotionDuration", detector.minMotionDuration);
    settings.setValue("minMotionIntensity", detector.minMotionIntensity);
    QRect qRoi(detector.roi.x, detector.roi.y, detector.roi.width, detector.roi.height);
    settings.setValue("roi", qRoi);
    settings.setValue("postBuffer", detector.postCapture);
    settings.setValue("scaleFrame", detector.scaleFrame);
    settings.endGroup();
}



// FUNCTIONS
int detectMotion(PacketSafeQueue& packetQueue, State& appState);
bool processVideoStream(LibavReader& reader, PacketSafeQueue& decodeQueue,
                        PacketSafeCircularBuffer& preCaptureBuffer, State& appState);
void resetWriter(State& appState, WriteState& writeState, LibavWriter& writer);
long long secondsWithoutError(State& appState);
void sigHandler(int signum);
void terminateThreads(PacketSafeQueue& packetQueue, PacketSafeCircularBuffer& buffer, State& appState);
void waitForMotion(State& appState);
bool writeDiagPicsToDisk(std::vector<MotionDiagPic>& diagPicBuffer);
int writeMotionPackets(PacketSafeCircularBuffer& buffer, State& appState);



// motion detection thread func -> decode, bgrsub, notify
int detectMotion(PacketSafeQueue& packetQueue, State& appState)
{
    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", thread motion started");
    // PerfCounter decode("decoding");
    // PerfCounter motion("motion detection");

    LibavDecoder decoder;
    int ret = decoder.open(appState.streamInfo.videoCodecParameters);
    if (ret < 0){
        avErrMsg("Failed to open codec", ret);
        return ret;
    }
    AVPacket* packet = nullptr;
    cv::Mat frame;

    MotionDetector detector;
    detector.bgrSubThreshold(appState.detector.bgrSubThreshold);       // foreground / background gray difference
    detector.minMotionDuration(appState.detector.minMotionDuration);   // consecutive frames
    detector.minMotionIntensity(appState.detector.minMotionIntensity); // pixels

    size_t npreIdxs = static_cast<size_t>(detector.minMotionDuration()) + 1;
    CircularBuffer<MotionDiagPic> diagBuffer(npreIdxs);


    while (!appState.terminate) {
        // decode queued packets, if new packets are available
        packetQueue.waitForNewPacket();
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", new packet received");
        if (appState.terminate) break;

        bool badDecode = false;
        size_t queueSize = packetQueue.size();

        while (packetQueue.pop(packet)) {
            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet " << cntDecoded << " popped, pts: " << packet->pts << ", size: " << packet->size);
            if (appState.terminate) break;
            // decode.startCount();
            if ((badDecode = !decoder.decodePacket(packet))) {
                std::cout << "Failed to decode packet for motion detection" << std::endl;
            }
            queueSize = (packetQueue.size() > queueSize) ? packetQueue.size() : queueSize;

            DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet decoded, queueSize: " << packetQueue.size());
            // decode.stopCount();

            av_packet_free(&packet);
        }

        /* DEBUG */
        if (queueSize > 10) {
            std::cout << getTimeStampMs() << " Queue of size: " << queueSize << " discharged at " << decoder.frameTime(appState.streamInfo.timeBase) << " sec" << std::endl;
        }

        // skip motion detection for partly decoded frames
        if (badDecode) continue;

        // retrieve last frame
        if (!decoder.retrieveFrame(frame)) {
            std::cout << "Failed to retrieve frame for motion detection" << std::endl;
        }
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", frame retrieved, time "  << decoder.frameTime(appState.streamInfo.timeBase) << " sec");

        // detect motion
        // motion.startCount();
        bool isMotion = detector.isContinuousMotion(frame);

        // buffer last frame for diagnostics
        // TODO integrate into MotionDetector class
        MotionDiagPic sd;
        sd.frame = detector.resizedFrame().clone();
        sd.motion = detector.motionMask().clone();
        sd.motionDuration = detector.motionDuration();
        sd.motionIntensity = detector.motionIntensity();
        diagBuffer.push(sd);

        if (isMotion) {
            if (!appState.motion.writeInProgress) {
                std::cout << getTimeStampMs() << " START MOTION ---------" << std::endl;
                if (appState.debug) createDiagPics(diagBuffer, appState.motionDiag);
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
        // motion.stopCount();

        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", motion detection finished");

        // detect motion dummy
        // std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", motion detection finished");
    }

    decoder.close();


    /* DEBUG
    decode.printStatistics();
    motion.printStatistics();
    //decode.printAllSamples();
    //motion.printAllSamples();
    */

    return 0;
}


bool processVideoStream(LibavReader& reader, PacketSafeQueue& decodeQueue, PacketSafeCircularBuffer& preCaptureBuffer, State& appState)
{
    bool succ = true;
    AVPacket* packetDecoder = nullptr;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << getTimeStampMs() << " Send SIGUSR1 to PID " << getpid() << " to terminate 'motion'" << std::endl;

    //reader.playStream();
    // int packetsRead = 0;
    while (succ) {

        // free packetDecoder in detectMotion thread
        if (!(succ = reader.readVideoPacket(packetDecoder))) break;

        //std::cout << "read packet " << cnt++ << ", pts: " << packet->pts << ", size: " << packet->size << std::endl;
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet read, pts: " << packetDecoder->pts );

        // clone packet for pre-capture buffer
        // free packetPreCaptureBuffer in writeMotionPackets thread
        AVPacket* packetPreCaptureBuffer = av_packet_clone(packetDecoder);
        if(!packetPreCaptureBuffer) {
            std::cout << "nullptr packet motion buffer -> break" << std::endl;
            break;
        }

        decodeQueue.push(packetDecoder);
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__ << ", packet pushed");

        preCaptureBuffer.push(packetPreCaptureBuffer);

        /* non-blocking getch
        // cannot run as background process if connected to terminal input
        // https://stackoverflow.com/questions/17621798/linux-process-in-background-stopped-in-jobs
        int pressedKey = rlutil::nb_getch();
        if (pressedKey == 27) {
            std::cout << std::endl << "<ESC> pressed, terminating video processing" << std::endl;
            appState.terminate = true;
            return false;
        }
        */

        // SIGUSR1 received
        if (g_terminate) {
            std::cout << std::endl << getTimeStampMs()
                      << " SIGUSR1 received, terminating 'motion'" << std::endl;
            appState.terminate = true;
            return false;
        }

        /* DEBUG
        ++packetsRead;
        if (!(packetsRead % 1500)) {
            std::cout << getTimeStampMs() << " packets read: " << packetsRead << std::endl;
        }
        */
    }
    std::cout << getTimeStampMs() << " Video processing discontinued" << std::endl;
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


void sigHandler(int signum)
{
    if (signum == SIGUSR1) {
        //std::cout << "SIGUSR1 received" << std::endl;
        g_terminate = 1;
    }
}


void terminateThreads(PacketSafeQueue& packetQueue, PacketSafeCircularBuffer& buffer, State& appState)
{
    rlutil::setColor(rlutil::RED);
    std::cout << getTimeStampMs() << " Terminate application"  << std::endl;
    rlutil::resetColor();

    appState.terminate = true;

    std::cout << getTimeStampMs() << " Waiting for worker threads to join"
              << std::endl;
    packetQueue.terminate();
    appState.threadMotionDetection.join();
    std::cout << getTimeStampMs() << " Motion detection thread joined" << std::endl;

    appState.motion.startCnd.notify_one();
    buffer.terminate();
    appState.threadWritePackets.join();
    std::cout << getTimeStampMs() << " Write packets thread joined" << std::endl;
}


void waitForMotion(State& appState)
{
    // wait for motion condition
    std::unique_lock<std::mutex> lock(appState.motion.startMtx);
    appState.motion.startCnd.wait(lock, [&]{return (appState.motion.start || appState.reset || appState.terminate);});
    appState.motion.start = false;
    appState.motion.writeInProgress = true;
}


bool writeDiagPicsToDisk(std::vector<MotionDiagPic>& diagBuf)
{
    std::string dirDiag = getTimeStamp(TimeResolution::sec_NoBlank);
    if (cv::utils::fs::exists(dirDiag) && cv::utils::fs::isDirectory(dirDiag)) {
        std::cout << "diag pic directory already exists: " << dirDiag << std::endl;
        return false;
    } else {
        if (cv::utils::fs::createDirectory(dirDiag)) {
            std::cout << "diag pic directory created: " << dirDiag << std::endl;
            for (auto diagSample : diagBuf) {
                std::string preIdx = std::to_string(diagSample.preIdx);

                // motion mask
                std::string fileNameMask = "mask " + preIdx + ".jpg";
                std::string filePathRelMask =  cv::utils::fs::join(dirDiag, fileNameMask);
                cv::imwrite(filePathRelMask, diagSample.motion);

                // original frame
                std::string fileNameOrig = "orig " + preIdx + ".jpg";
                std::string filePathOrig =  cv::utils::fs::join(dirDiag, fileNameOrig);
                cv::imwrite(filePathOrig, diagSample.frame);
            } //end for
            return true;
        } else {
            std::cout << "cannot create diag pic directory: " << dirDiag << std::endl;
            return false;
        }
    }
}


// demux from buffer thread func -> write video packets, if motion present
int writeMotionPackets(PacketSafeCircularBuffer& buffer, State& appState)
{
    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", thread write packets started");
    WriteState writeState = WriteState::open;

    LibavWriter writer;
    writer.init();
    int postCapture = appState.detector.postCapture;

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

            // write debug pics to disk
            if (appState.debug) {
                if (!writeDiagPicsToDisk(appState.motionDiag)) {
                    std::cout << "not able to save diag pics to disk at "
                        << getTimeStamp(TimeResolution::sec_NoBlank) << std::endl;
                }
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
                av_packet_free(&packet);
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", key frame written");
            } else {
                std::cout << "no key frame found -> close output file" << std::endl;
                writeState = WriteState::close;
            }

            // drain pre-capture buffer
            while(buffer.pop(packet)) {
                writer.writeVideoPacket(packet);
                av_packet_free(&packet);
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
                av_packet_free(&packet);
                DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", packet written");

                // reset -> open
                if (appState.terminate || appState.reset) {
                    resetWriter(appState, writeState, writer);
                    DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", write: reset -> open");
                    break;
                // no motion -> writePostCapture (only if no reset)
                } else if (appState.motion.stop) {
                    postCapture = appState.detector.postCapture;
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
                av_packet_free(&packet);
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



int main(int argc, char *argv[])
{
    std::signal(SIGUSR1, sigHandler);

    QCoreApplication a(argc, argv);
    QCoreApplication::setOrganizationName("grzonka");
    // settings file
    // linux:   ~/.config/grzonka/motion.conf
    // windows: HKEY_CURRENT_USER\Software\grzonka\tamper
    Params params;

    // parse command line args
    QCommandLineParser cmdLine;
    cmdLine.setApplicationDescription("Extract motion sequences of video stream to local disk");
    cmdLine.addHelpOption();
    QCommandLineOption roiOption(QStringList() << "r" << "roi", "show roi before processing files");
    cmdLine.addOption(roiOption);
    cmdLine.addPositionalArgument("rtsp://...", "Input video stream");

    cmdLine.process(a);
    const QStringList posArgs = cmdLine.positionalArguments();

    if (posArgs.size() > 0) {

    } else {
        std::cout << "No input stream was specified" << std::endl;
        cmdLine.showHelp();
    }

    avformat_network_init();
    LibavReader reader;
    reader.init();

    // TODO check input stream before opening
    std::string inputStream = posArgs.at(0).toStdString();
    int ret = reader.open(inputStream);
    if (ret < 0) {
        std::cout << getTimeStampMs() << " Error opening input: " << inputStream << std::endl;
        return -1;
    } else {
        std::cout << getTimeStampMs() << " Open video input: "  << inputStream << std::endl;
    }

    // prepare for motion detection thread
    // TODO refactor State as class with initializing valid state
    State appState;

    // foreground / background gray difference
    appState.detector.bgrSubThreshold = params.detector.bgrSubThreshold;

    // consecutive frames
    appState.detector.minMotionDuration = params.detector.minMotionDuration;

    // pixels
    appState.detector.minMotionIntensity = params.detector.minMotionIntensity;

    // frames
    appState.detector.postCapture = params.detector.postCapture;

    appState.motion.start = false; // needs lock, if detection thread is already running
    appState.motion.stop = true;
    appState.motion.writeInProgress = false;

    appState.reset = false;
    appState.resetDone = false;
    appState.terminate = false;

    if (!reader.getVideoStreamInfo(appState.streamInfo)) {
        std::cout << getTimeStampMs() << "Failed to get video stream info" << std::endl;
        return -1;
    }
    appState.errorCount = 0;
    appState.timeLastError = std::chrono::system_clock::now();

    PacketSafeQueue decodeQueue;
    appState.threadMotionDetection = std::thread(detectMotion, std::ref(decodeQueue), std::ref(appState));
    // std::thread threadMotionDetection(detectMotion, std::ref(decodeQueue), std::ref(appState));

    PacketSafeCircularBuffer preCaptureBuffer(100);
    appState.threadWritePackets = std::thread(writeMotionPackets, std::ref(preCaptureBuffer), std::ref(appState));
    // std::thread threadWritePackets(writeMotionPackets, std::ref(preCaptureBuffer), std::ref(appState));

    // reader.openPaused()
     while (!appState.terminate) {
        if (!reader.isOpen()) {
            ret = -1;
            while (ret) {
                ret = reader.open(inputStream);

                // keep trying to connect
                // 2021-11-11 also for invalid data (for test purposes)
                // if (ret == AVERROR(ETIMEDOUT) || ret == AVERROR(ENETUNREACH) || ret == AVERROR_INVALIDDATA) {
                // keep trying to connect for all errors
                if (ret < 0) {
                    int timeout = 30;
                    std::cout << getTimeStampMs() << " Open input error (" << ret
                              << "), trying to re-connect in" << timeout << " sec" << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(timeout));
                    continue;
                }
            }
            std::cout << getTimeStampMs() << " Video input opened successfully: "
                      << inputStream << std::endl;
            if (!reader.getVideoStreamInfo(appState.streamInfo)) {
                std::cout << getTimeStampMs() << " Failed to get video stream info"
                          << std::endl;
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
        // DEBUG TODO DELETE
        std::cout << getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", waiting for resetDone" << std::endl;
        std::unique_lock<std::mutex> lockReset(appState.resetDoneMtx);
        appState.resetDoneCnd.wait(lockReset, [&]{return(appState.resetDone);});
        appState.resetDone = false;
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", resetDone triggered");
        // DEBUG TODO DELETE
        std::cout << getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", resetDone triggered" << std::endl;

        reader.close();
        DEBUG(getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", reader closed");
        // DEBUG TODO DELETE
        std::cout << getTimeStampMs() << " " << __func__ << " #" << __LINE__<< ", reader closed" << std::endl;

        std::cout << getTimeStampMs() << " Reading errors: " << ++appState.errorCount
                  << ", seconds since last error: " << secondsWithoutError(appState) << std::endl;
        int timeoutReset = 30;
        std::cout << getTimeStampMs() << " Buffers have been reset, trying to re-connect in "
                  << timeoutReset << " sec" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(timeoutReset));
    }

    terminateThreads(decodeQueue, preCaptureBuffer, appState);
    return 0;
}
