#ifndef LIBAVREADWRITE_H
#define LIBAVREADWRITE_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <opencv2/opencv.hpp>

#include <string>


void errLog(const char * file, int line, std::string msg, int error = 0);

// ##__VA_ARGS is a hack for gcc to get rid of the trailing comma
// it will vanish with C++20 when __VA_OPT__ becomes available
// https://stackoverflow.com/questions/5588855/standard-alternative-to-gccs-va-args-trick
#define avErrMsg( msg, ... ) errLog( __FILE__, __LINE__, msg, ##__VA_ARGS__ )

struct VideoStream
{
    AVCodecParameters*      videoCodecParameters;
    AVRational              frameRate;
    AVRational              timeBase;
};

class LibavDecoder
{
public:
    LibavDecoder();
    ~LibavDecoder();
    void                close();
    bool                decodePacket(AVPacket* packet);
    double              frameTime(AVRational timeBase);
    int                 open(AVCodecParameters* vCodecParams);
    bool                retrieveFrame(cv::Mat& grayImage);
private:
    AVCodec             *m_codec;
    AVCodecContext      *m_codecCtx;
    AVCodecParameters   *m_codecParams;
    AVFrame             *m_frame;
};

class LibavReader
{
public:
    LibavReader();
    ~LibavReader();
    AVPacket*           cloneVideoPacket();
    void                close();
    bool                decodePacket(AVPacket* packet);
    bool                getVideoStreamInfo(VideoStream& videoStreamInfo);
    int                 init();
    int                 open(std::string file);
    bool                readVideoPacket(AVPacket*& pkt);
private:
    AVFormatContext*    m_inCtx;
    int                 m_idxVideoStream; // assumption: there is only one video stream
    AVPacket*           m_packet;
};


class LibavWriter
{
public:
    LibavWriter();
    ~LibavWriter();
    void                close();
    int                 init();
    int                 open(std::string file, VideoStream videoStreamInfo);
    bool                writeVideoPacket(AVPacket *packet);
private:
    bool                setFrameRate(AVRational fps);
    bool                setTimeBase(AVRational timeBase);
    int                 m_idxVideoStream; // assumption: there is only one video stream
    bool                m_isOpen;
    AVFormatContext*    m_outCtx;
    AVStream*           m_outStream;
    int64_t             m_packetCount;
};

#endif // LIBAVREADWRITE_H
