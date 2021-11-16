#ifndef MOTIONDETECTOR_H
#define MOTIONDETECTOR_H
#include "backgroundsubtraction.h"
#include "circularbuffer.h"
#include "perfcounter.h"

#include <opencv2/opencv.hpp>

enum class MotionMinimal {intensity, duration};
enum class Duration {frames, seconds};

// TODO
// setMotionArea - in per cent of frame area
// setMotionDuration - in sec, in number of frames (alternative)
// setMotionRoi - in cv::Rect


// CLASSES
struct DetectorParams
{
    double      bgrSubThreshold;
    int         minMotionDuration;
    int         minMotionIntensity;
    int         postCapture; // preCapture determined by key frame distance
    int         preCapture;  // reseved for future usage
    cv::Rect    roi;
    double      scaleFrame;
    bool        debug;
    char        avoidPaddingWarning1[7];
};


class MotionDetector
{
public:
    MotionDetector();
    /* background subtractor: threshold of frame difference */
    void        bgrSubThreshold(double threshold);
    double      bgrSubThreshold() const;
    bool        hasFrameMotion(cv::Mat frame);
    bool        isContinuousMotion(cv::Mat frame);
    /* duration as number of update steps */
    void        minMotionDuration(int value);
    int         minMotionDuration() const;
    int         motionDuration() const;
    /* intensity area as pixels
     * TODO: as per cent of frame area
     */
    void        minMotionIntensity(int value);
    int         minMotionIntensity() const;
    int         motionIntensity() const;
    cv::Mat     motionMask() const;
    cv::Mat     processedFrame() const;
    void        resetBackground();
    cv::Mat     resizedFrame() const;
    /* region of interest related to upper left corner */
    void        roi(cv::Rect);
    cv::Rect    roi() const;
    // TODO reset backgroundsubtractor
private:
    cv::Ptr<BackgroundSubtractorLowPass> m_bgrSub;
    bool        m_isContinuousMotion;
    int         m_minMotionDuration;
    int         m_minMotionIntensity;
    int         m_motionDuration;
    int         m_motionIntensity;
    cv::Mat     m_motionMask;
    cv::Mat     m_resizedFrame;
    cv::Mat     m_processedFrame;
    cv::Rect    m_roi;
};


struct MotionDiagPic
{
    cv::Mat frame;
    cv::Mat motion;
    int     motionDuration;
    int     motionIntensity;
    int     preIdx;
};



// FUNCTIONS
bool createDiagPics(CircularBuffer<MotionDiagPic>& diagBuf, std::vector<MotionDiagPic>& diagPicBuffer);
void printDetectionParams(MotionDiagPic& diagSample);
void showDiagPics(std::vector<MotionDiagPic>& diagPicBuffer);



#endif // MOTIONDETECTOR_H
