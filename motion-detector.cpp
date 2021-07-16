#include "motion-detector.h"

MotionDetector::MotionDetector() :
    m_perfPre("pre-process"),
    m_perfApply("bgrSub"),
    m_perfPost("cntNonZero"),

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
    // use full frame size, if roi is not initialized yet
    if (m_roi == cv::Rect(0,0,0,0))
        m_roi = cv::Rect(cv::Point(0,0), frame.size());

    // pre-processing of clipped frame
    cv::Mat processedFrame;
    m_perfPre.startCount();
    /* performance for pre-processing HD frame on RPi:
     * blur10x10: 20ms      bgrSub: 25ms
     * resize0.5:  2ms      bgrSub:  5ms
     * */
    //cv::blur(frame(m_roi), processedFrame, cv::Size(10,10));
    cv::resize(frame, processedFrame, cv::Size(), 0.25, 0.25, cv::INTER_LINEAR);
    //cv::resize(frame, processedFrame, cv::Size(), 0.5, 0.5, cv::INTER_NEAREST);
    m_perfPre.stopCount();

    // detect motion in current frame
    m_perfApply.startCount();
    m_bgrSub->apply(processedFrame, m_motionMask);
    //m_bgrSub->apply(frame, m_motionMask);

    m_perfApply.stopCount();
    m_perfPost.startCount();
    int motionIntensity = cv::countNonZero(m_motionMask);
    bool isMotion = motionIntensity > m_minMotionIntensity ? true : false;

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
    m_perfPost.stopCount();

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


cv::Mat MotionDetector::motionMask() const
{
    return m_motionMask;
}


void MotionDetector::roi(cv::Rect value)
{
    m_roi = value;
}


cv::Rect MotionDetector::roi() const
{
    return m_roi;
}
