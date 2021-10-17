#include "libavreadwrite.h"

#include <opencv2/opencv.hpp>

#include <iostream>


int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cout << "usage: avreadwrite videofile.mp4" << std::endl;
        return -1;
    }

    // instantiate reader, open video file and get stream info
    LibavReader reader;
    reader.init();
    int ret = reader.open(argv[1]);
    if (ret < 0) {
        std::cout << "Error opening input: " << argv[1] << std::endl;
        return -1;
    } else {
        std::cout << "Video input opened successfully: "  << argv[1] << std::endl;
    }
    VideoStream streamInfo;
    if (!reader.getVideoStreamInfo(streamInfo)) {
        std::cout << "Failed to get video stream info" << std::endl;
        return -2;
    }

    // instantiate decoder
    LibavDecoder decoder;
    ret = decoder.open(streamInfo.videoCodecParameters);
    if (ret < 0){
        avErrMsg("Failed to open codec", ret);
        return ret;
    }

    std::cout << "Hit <ESC> to terminate video processing" << std::endl;
    AVPacket* packet = nullptr;
    cv::Mat frame;
    while (true)
    {
        if (!reader.readVideoPacket(packet)) {
            std::cout << "Failed to read packet" << std::endl;
            break;
        }
        if (!decoder.decodePacket(packet)) {
            av_packet_free(&packet);
            continue;
        }
        av_packet_free(&packet);

        if (!decoder.retrieveFrame(frame)) {
            std::cout << "Failed to retrieve frame" << std::endl;
            break;
        }
        cv::imshow("frame", frame);
        if (cv::waitKey(10) == 27)
            break;
    }

    return 0;
}
