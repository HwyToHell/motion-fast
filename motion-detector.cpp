#include "motion-detector.h"


// CLASS IMPLEMENTATION
MotionDetector::MotionDetector() :
    m_isContinuousMotion{false},
    m_minMotionDuration{10},    // number of consecutive frames with motion
    m_minMotionIntensity{100},  // number of pixels with motion
    m_motionDuration{0},
    m_roi{0,0,0,0}
{
    // default -> alpha: 0.005 threshold: 50
    m_bgrSub = createBackgroundSubtractorLowPass(0.005, 50);

}


void MotionDetector::bgrSubThreshold(double threshold)
{
    /* limit between 0 an 100 */
    threshold = threshold > 100 ? 100 : threshold;
    threshold = threshold < 0 ? 0 : threshold;
    m_bgrSub->threshold(threshold);
}


double MotionDetector::bgrSubThreshold() const
{
    return m_bgrSub->threshold();
}


bool MotionDetector::hasFrameMotion(cv::Mat frame)
{
    /* frame must be gray scale for this optimized version
     * of background subtractor to work */
    assert(frame.channels() == 1);

    // use full frame size, if roi is not initialized yet
    if (m_roi == cv::Rect(0,0,0,0))
        m_roi = cv::Rect(cv::Point(0,0), frame.size());

    // pre-processing of clipped frame
    /* performance for pre-processing HD frame on RPi:
     * blur10x10: 20ms      bgrSub: 25ms
     * resize0.5:  2ms      bgrSub:  5ms
     * */

    // intermediate step: resize full HD frame 1920x1080 -> 480x270
    cv::resize(frame, m_resizedFrame, cv::Size(), 0.25, 0.25, cv::INTER_LINEAR);
    // remove noise by blurring
    int kernel = m_resizedFrame.size().width / 96;
    if (kernel == 0) kernel = 2;
    cv::blur(m_resizedFrame, m_processedFrame, cv::Size(kernel,kernel));

    // detect motion in current frame
    //m_bgrSub->apply(m_resizedFrame, m_motionMask);
    m_bgrSub->apply(m_processedFrame, m_motionMask);

    m_motionIntensity = cv::countNonZero(m_motionMask);
    bool isMotion = m_motionIntensity > m_minMotionIntensity ? true : false;

    // DEBUG
    /*
    std::cout << "intensitiy: " << motionIntensity << "  " << isMotion << std::endl;
    std::cout << "m_motionMask channels: " << m_motionMask.channels() << " size: " << m_motionMask.size << std::endl;
    // color motionMask based on intensity
    cv::Mat whiteBgr(m_roi.height, m_roi.width, CV_8UC3, cv::Scalar(255,255,255));
    cv::Mat redBgr(m_roi.height, m_roi.width, CV_8UC3, cv::Scalar(0,0,255));
    cv::Mat coloredMask(m_roi.height, m_roi.width, CV_8UC3, cv::Scalar(0,0,0));
    if (isMotion) {
        // motion mask in red
        cv::bitwise_and(redBgr, redBgr, coloredMask, m_motionMask);
    } else {
        // motion mask in white
        cv::bitwise_and(whiteBgr, whiteBgr, coloredMask, m_motionMask);
    }
    cv::imshow("motion mask", m_motionMask);
    cv::imshow("colored mask", coloredMask);
    */
    // DEBUG_END

    // update motion duration
    // motion increase by 1
    if (isMotion) {
        ++m_motionDuration;
        m_motionDuration = m_motionDuration > m_minMotionDuration
                ? m_minMotionDuration : m_motionDuration;

    // no motion decrease by 1
    } else {
        --m_motionDuration;
        m_motionDuration = m_motionDuration <= 0
                ? 0 : m_motionDuration;
    }

    return isMotion;
}


bool MotionDetector::isContinuousMotion(cv::Mat frame)
{
    hasFrameMotion(frame);

    if (m_motionDuration >= m_minMotionDuration) {
        m_isContinuousMotion = true;
    } else if (m_motionDuration == 0) {
        m_isContinuousMotion = false;
    }

    return m_isContinuousMotion;
}


void MotionDetector::minMotionDuration(int value)
{
    /* allow 300 update steps at max */
    value = value > 300 ? 300 : value;
    value = value < 0 ? 0 : value;
    m_minMotionDuration = value;
}


int MotionDetector::minMotionDuration() const
{
    return m_minMotionDuration;
}


void MotionDetector::minMotionIntensity(int value)
{
    //* per cent of frame area */
    //value = value > 100 ? 100 : value;
    // value = value < 0 ? 0 : value;
    /* number of pixels */
    m_minMotionIntensity = value;
}


int MotionDetector::minMotionIntensity() const
{
    return m_minMotionIntensity;
}


int MotionDetector::motionDuration() const
{
    return m_motionDuration;
}


int MotionDetector::motionIntensity() const
{
    return m_motionIntensity;
}


cv::Mat MotionDetector::motionMask() const
{
    return m_motionMask;
}


cv::Mat MotionDetector::processedFrame() const
{
    return m_processedFrame;
}


cv::Mat MotionDetector::resizedFrame() const
{
    return m_resizedFrame;
}


void MotionDetector::roi(cv::Rect value)
{
    m_roi = value;
}


cv::Rect MotionDetector::roi() const
{
    return m_roi;
}



// FUNCTION IMPLEMENTATION
bool createDiagPics(CircularBuffer<MotionDiagPic>& diagBuf, std::vector<MotionDiagPic>& diagPicBuffer)
{
    diagPicBuffer.clear();
    size_t nSections = 4;
    if (diagBuf.size() < nSections) {
        std::cout << "diag buffer too small: " << diagBuf.size() << " elements" << std::endl;
        return false;
    } else {
        size_t idxSteps = diagBuf.size() / (nSections);
        for (size_t n = 0; n <= nSections; ++n) {
            size_t idxRingBuf = (n * idxSteps);

            // preIdx -> reverse index of circular buffer (head = oldest index)
            int preIdx = - static_cast<int>((diagBuf.size() - 1) - (n * idxSteps));
            // std::cout << "pre idx: " << preIdx << std::endl;
            diagBuf.at(idxRingBuf).preIdx = preIdx;

            printDetectionParams(diagBuf.at(idxRingBuf));
            diagPicBuffer.push_back(diagBuf.at(idxRingBuf));
        }
        return true;
    }
}


void printDetectionParams(MotionDiagPic& diagSample)
{
    const cv::Scalar blue(255,0,0);
    const cv::Scalar green(0,255,0);
    const cv::Scalar red(0,0,255);
    int fontFace = cv::HersheyFonts::FONT_HERSHEY_SIMPLEX;
    double fontScale = static_cast<double>(diagSample.frame.size().height) / 500;
    int fontThickness = 1;

    int baseLine;
    int fontHeight = cv::getTextSize("0", fontFace, fontScale, fontThickness,
                                     &baseLine).height;
    int orgX = diagSample.frame.size().width / 25;
    int orgY = diagSample.frame.size().height / 5;
    int spaceY = diagSample.frame.size().height / 30;

    if (diagSample.frame.channels() == 1) {
        cv::cvtColor(diagSample.frame, diagSample.frame, cv::COLOR_GRAY2BGR);
    }

    // duration
    cv::Point orgDuration(orgX, orgY);
    std::stringstream ssDuration;
    ssDuration << "duration: " << diagSample.motionDuration;
    cv::putText(diagSample.frame, ssDuration.str(), orgDuration, fontFace,
                fontScale, red, fontThickness);

    // intensity
    cv::Point orgIntensity(orgX, orgY + fontHeight + spaceY);
    std::stringstream ssIntensity;
    ssIntensity << "intensity: " << diagSample.motionIntensity;
    cv::putText(diagSample.frame, ssIntensity.str(), orgIntensity, fontFace,
                fontScale, red, fontThickness);
}


void showDiagPics(std::vector<MotionDiagPic>& diagPicBuffer)
{
    for (auto diagSample : diagPicBuffer) {
        auto idx = std::to_string(diagSample.preIdx);
        cv::imshow("frame " + idx, diagSample.frame);
    }
}
