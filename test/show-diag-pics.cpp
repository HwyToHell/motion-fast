#include "../motion-detector.h"
#include "../circularbuffer.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <sstream>

// defined in motion-fast
bool createDiagPics(CircularBuffer<MotionDiagPic>& diagBuf, std::vector<MotionDiagPic>& diagPicBuffer);


void printFrameNum(cv::Mat& frame, int num)
{
    const cv::Scalar blue(255,0,0);
    const cv::Scalar green(0,255,0);
    const cv::Scalar red(0,0,255);
    int font = cv::HersheyFonts::FONT_HERSHEY_SIMPLEX;
    double fontScale = 1;
    cv::Point org(10, 40);
    std::stringstream ss;
    ss << "frame:    " << num;
    cv::putText(frame, ss.str(), org, font, fontScale, green, 2);
}


void printParams(cv::VideoCapture& vidCap) {
    using namespace std;
    cout << "backend name: " << vidCap.getBackendName() << endl;
    cout << "backend #:    " << vidCap.get(cv::CAP_PROP_BACKEND) << endl;
    cout << "mode:         " << vidCap.get(cv::CAP_PROP_MODE) << endl;
    cout << "fps:          " << vidCap.get(cv::CAP_PROP_FPS) << endl;
    cout << "width:        " << vidCap.get(cv::CAP_PROP_FRAME_WIDTH) << endl;
    cout << "height:       " << vidCap.get(cv::CAP_PROP_FRAME_HEIGHT) << endl << endl;
    return;
}


void showDiag(std::vector<MotionDiagPic>& diag)
{
    if (diag.size() == 0) {
        std::cout << "no diag images available" << std::endl;
        return;
    }

    int screenWidth = 1920;
    // int screenHeight = 1080;
    int winWidth = (screenWidth / static_cast<int>(diag.size())) - 10;
    int winHeight = winWidth * 10 / 16;

    int n = 0;
    for (auto diagSample : diag) {
        auto idx = std::to_string(diagSample.preIdx);
        int xOrg = n * (winWidth + 4);

        // show frame
        int yOrg = 0;
        std::string caption_org = "org: frame " + idx;
        cv::namedWindow(caption_org, cv::WINDOW_NORMAL);
        cv::imshow(caption_org, diagSample.frame);
        cv::moveWindow(caption_org, xOrg, yOrg);
        cv::resizeWindow(caption_org, winWidth, winHeight);


        // show motion mask
        yOrg = winHeight + 70;
        std::string caption_mask = "mask: frame " + idx;
        cv::namedWindow(caption_mask, cv::WINDOW_NORMAL);
        cv::imshow(caption_mask, diagSample.motion);
        cv::moveWindow(caption_mask, xOrg, yOrg);
        cv::resizeWindow(caption_mask, winWidth, winHeight);

        ++n;
    }
}



int main_show_diag_pics(int argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    MotionDetector detector;
    detector.bgrSubThreshold(50);       // foreground / background gray difference
    detector.minMotionDuration(30);     // consecutive frames
    detector.minMotionIntensity(1000);

    cv::VideoCapture cap;
    if (!cap.open(0)) {
        std::cout << "cannot open cam" << std::endl;
        return -1;
    }
    // printParams(cap);
    // return 0;

    std::vector<MotionDiagPic>       motionDiag;
    CircularBuffer<MotionDiagPic>    diagBuffer(31);
    cv::Mat frame, gray;
    std::cout << "Hit ESC to break" << std::endl;

    // equalize brightness changes when starting cam
    for (int i = 0; i < 30; ++i)
        cap.read(frame);

    int n = 0;
    while(cap.read(frame)) {
        ++n;
        // pic must be grayscale
        cv::cvtColor(frame, gray,cv::COLOR_BGR2GRAY);
        // cv::imshow("frame", gray);

        // put time stamp or number on pic
        printFrameNum(frame, n);
        cv::Mat resized, blurred;
        cv::resize(frame, resized, cv::Size(), 0.5, 0.5, cv::INTER_LINEAR);
        cv::blur(resized, blurred, cv::Size(5,5));
        cv::imshow("frame", blurred);
        //if (cv::waitKey(20) == 27) break;
        //continue;

        // detect motion
        bool isMotion = detector.isContinuousMotion(gray);

        // buffer last frame for diagnostics
        MotionDiagPic sd;
        sd.frame = detector.processedFrame().clone();
        sd.motion = detector.motionMask().clone();
        sd.motionDuration = detector.motionDuration();
        sd.motionIntensity = detector.motionIntensity();
        sd.preIdx = n;
        diagBuffer.push(sd);

        if (isMotion) {
            std::cout << "--- START MOTION ---------" << std::endl;
            createDiagPics(diagBuffer, motionDiag);
            break;
        }

        if (cv::waitKey(20) == 27) break;
    }

    showDiag(motionDiag);

    std::cout << "Press any key to finish" << std::endl;
    cv::waitKey(0);

    return 0;
}
