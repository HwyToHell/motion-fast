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



class LibavReader
{
public:
    LibavReader();
    ~LibavReader();
    AVPacket*           cloneVideoPacket();
    void                close();
    bool                decodePacket(AVPacket* packet);
    AVRational          frameRate();
    int                 init();
    int                 open(std::string file);
    bool                readVideoPacket();
    bool                retrieveFrame(cv::Mat& frame);
    AVRational          timeBase();
    AVCodecParameters*  videoCodecParams();
private:
    AVCodec             *codec;
    AVCodecContext      *codecCtx;
    AVCodecParameters   *codecParams;
    AVFrame             *frame;
    AVFormatContext     *ic;
    int                 idxVideoStream; // assumption: there is only one video stream
    AVPacket            *packet;
};


class LibavWriter
{
public:
    LibavWriter();
    ~LibavWriter();
    void    close();
    bool    frameRate(AVRational fps);
    int     init();
    int     open(std::string file, AVCodecParameters* vCodecParams);
    bool    timeBase(AVRational timeBase);
    bool    writeVideoPacket(AVPacket *packet);
private:
    int             idxVideoStream; // assumption: there is only one video stream
    bool            isOpen;
    AVFormatContext *oc;
    AVStream        *os;
    int64_t         packetCount;
};

#endif // LIBAVREADWRITE_H
