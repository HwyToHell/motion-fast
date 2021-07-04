#include "libavreadwrite.h"


void errLog(const char * file, int line, std::string msg, int avError)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    size_t errbufSize = AV_ERROR_MAX_STRING_SIZE;
    std::string avErrString("");

    std::cerr << msg;
    if (avError) {
        if (av_strerror(avError, errbuf, errbufSize) == 0) {
            avErrString.assign(errbuf);
        } else {
            avErrString = "AVERROR: " + std::to_string(avError);
        }
        std::cerr << ": " << avErrString;
    }
    std::cerr << std::endl << "  (" << file << ":" << line <<")" << std::endl;
}



/*** LibavDecoder ************************************************************/
LibavDecoder::LibavDecoder() :
    m_codec(nullptr),
    m_codecCtx(nullptr),
    m_codecParams(nullptr),
    m_frame(nullptr)
{
    avcodec_register_all();
}


LibavDecoder::~LibavDecoder()
{
    // =dealloc private vars
    close();
}


void LibavDecoder::close()
{

    av_frame_free(&m_frame);

    // free open or closed codecs
    // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#gaf869d0829ed607cec3a4a02a1c7026b3
    avcodec_free_context(&m_codecCtx);
}


bool LibavDecoder::decodePacket(AVPacket* packet)
{
    int ret = avcodec_send_packet(m_codecCtx, packet);
    if (ret < 0) {
        avErrMsg("Failed to send packet to decoder", ret);
        return false;
    }

    ret = avcodec_receive_frame(m_codecCtx, m_frame);
    if (ret == 0) {
        return true; // frame available
    } else if (ret != AVERROR(EAGAIN)) {
        avErrMsg("Failed to receive frame from decoder", ret);
        return false;
    }

    return false; // no frame was returned, read next packet
}


int LibavDecoder::open(AVCodecParameters* vCodecParams)
{
    m_codecParams = vCodecParams;
    int ret = -1;

    m_codec = avcodec_find_decoder(m_codecParams->codec_id);
    if (!m_codec) {
        avErrMsg("Unsupported codec");
        return ret;
    }

    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) {
        avErrMsg("Failed to allocate memory for AVCodecContext");
        return ret;
    }

    ret = avcodec_parameters_to_context(m_codecCtx, m_codecParams);
    if (ret < 0) {
        avErrMsg("Failed to copy codec params to codec context", ret);
        avcodec_free_context(&m_codecCtx);
        return ret;
    }

    ret = avcodec_open2(m_codecCtx, m_codec, nullptr);
    if (ret < 0) {
        avErrMsg("Failed to open codec", ret);
        avcodec_free_context(&m_codecCtx);
        return ret;
    }

    m_frame = av_frame_alloc();
    if (!m_frame) {
        avErrMsg("Failed to allocate memory for AVFrame");
        avcodec_free_context(&m_codecCtx);
        return -1;
    }

    return 0;
}


bool LibavDecoder::retrieveFrame(cv::Mat& grayImage)
{
    if (!m_frame->width || !m_frame->height) {
        return false;
    } else {
        cv::Size frameSize(m_frame->width, m_frame->height);
        grayImage = cv::Mat(frameSize, CV_8UC1, m_frame->data[0]);
        return true;
    }
}



/*** LibavReader *************************************************************/

LibavReader::LibavReader() :
    codec(nullptr),
    frame(nullptr),
    ic(nullptr),
    idxVideoStream(-1),
    packet(nullptr)
{

}


LibavReader::~LibavReader()
{
    close();
}


void freeOpenReader(AVFormatContext *ic, AVCodecContext *codecCtx) {
    avcodec_free_context(&codecCtx);
    avformat_close_input(&ic);
    avformat_free_context(ic);
}


AVPacket* LibavReader::cloneVideoPacket()
{
    return av_packet_clone(packet);
}


void LibavReader::close()
{
    av_frame_free(&frame);
    av_packet_free(&packet);
    freeOpenReader(ic, codecCtx);
    idxVideoStream = -1;
}


bool LibavReader::decodePacket(AVPacket* packet)
{
    if (idxVideoStream < 0) {
        avErrMsg("VideoReader must be opened before decoding packets");
        return false;
    }

    int ret = avcodec_send_packet(codecCtx, packet);
    if (ret < 0) {
        avErrMsg("Failed to send packet to decoder", ret);
        return false;
    }

    ret = avcodec_receive_frame(codecCtx, frame);
    if (ret == 0) {
        return true; // frame available
    } else if (ret != AVERROR(EAGAIN)) {
        avErrMsg("Failed to receive frame from decoder", ret);
        return false;
    }

    return false; // no frame was returned, read next packet
}


AVRational LibavReader::frameRate()
{
    if (idxVideoStream >= 0) {
        return av_stream_get_r_frame_rate(ic->streams[idxVideoStream]);
    } else {
        return AVRational {0,0};
    }
}


AVCodecParameters* LibavReader::getVideoCodecParams()
{
    if (idxVideoStream >= 0) {
        return (ic->streams[idxVideoStream]->codecpar);
    } else {
        return nullptr;
    }
}


AVPacket* LibavReader::getVideoPacket()
{
    return packet;
}


VideoTiming LibavReader::getVideoTiming()
{
    VideoTiming timing;
    timing.frameRate = idxVideoStream >= 0 ? av_stream_get_r_frame_rate(ic->streams[idxVideoStream]) : AVRational {0,0};
    timing.timeBase = idxVideoStream >= 0 ? ic->streams[idxVideoStream]->time_base : AVRational {0,0};
    return timing;
}


int LibavReader::init()
{
    av_register_all();

    return 0;
}


int LibavReader::open(std::string fileName)
{
    int ret = -1;

    ic = avformat_alloc_context();

    ret = avformat_open_input(&ic, fileName.c_str(), nullptr, nullptr);
    if (ret < 0) {
        avErrMsg("Failed to open input", ret);
        freeOpenReader(ic, codecCtx);
        return ret;
    }

    ret = avformat_find_stream_info(ic, nullptr);
    if (ret < 0) {
        avErrMsg("Failed to retrieve input stream information", ret);
        freeOpenReader(ic, codecCtx);
        return ret;
    }

    // get video stream and decoder
    idxVideoStream = -1;
    for (unsigned int i = 0; i < ic->nb_streams; ++i) {
        AVStream *inStream = ic->streams[i];
        AVCodecParameters *inCodecPar = inStream->codecpar;
        if (inCodecPar->codec_type == AVMEDIA_TYPE_VIDEO) {
            idxVideoStream = static_cast<int>(i);
            codec = avcodec_find_decoder(inCodecPar->codec_id);
            if (!codec) {
                avErrMsg("Unsupported codec for video stream");
                idxVideoStream = -2;
            }
            codecParams = inCodecPar;
        }
    }
    if (idxVideoStream < 0) {
        avErrMsg("Video input does not contain a video stream or unsupported codec");
        freeOpenReader(ic, codecCtx);
        return ret;
    }

    // TODO move to decoder
    // get codec context and open codec
    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        avErrMsg("Failed to allocate memory for AVCodecContext");
        freeOpenReader(ic, codecCtx);
        return ret;
    }
    ret = avcodec_parameters_to_context(codecCtx, codecParams);
    if (ret < 0) {
        avErrMsg("Failed to copy codec params to codec context", ret);
        freeOpenReader(ic, codecCtx);
        return ret;
    }
    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        avErrMsg("Failed to open codec", ret);
        freeOpenReader(ic, codecCtx);
        return ret;
    }

    // alloc packet and frame struct
    packet = av_packet_alloc();
    if (!packet) {
        avErrMsg("Failed to allocate memory for AVPacket");
        return -1;
    }
    frame = av_frame_alloc();
    if (!frame) {
        avErrMsg("Failed to allocate memory for AVFrame");
        return -1;
    }

    return 0;
}


bool LibavReader::readVideoPacket()
{
    av_packet_unref(packet);

    int ret = -1;
    // read videostream only
    for (unsigned int i = 0; i < ic->nb_streams; i++) {
        ret = av_read_frame(ic, packet);
        if (ret == AVERROR_EOF) {
            std::cout << "end of file" << std::endl;
            return false;
        } else if (ret < 0) {
            avErrMsg("Failed to read packet", ret);
            return false;
        }

        if (packet->stream_index == idxVideoStream) {
            return true;
        }
    }
    return false;
}


bool LibavReader::readVideoPacket2(AVPacket*& pkt)
{
    pkt = nullptr;
    int ret = -1;
    // read videostream only
    for (unsigned int i = 0; i < ic->nb_streams; i++) {
        ret = av_read_frame(ic, packet);
        if (ret == AVERROR_EOF) {
            std::cout << "end of file" << std::endl;
            return false;
        } else if (ret < 0) {
            avErrMsg("Failed to read packet", ret);
            return false;
        }

        if (packet->stream_index == idxVideoStream) {
            pkt = av_packet_clone(packet);
            av_packet_unref(packet);
            return true;
        }
    }
    return false;
}


bool LibavReader::retrieveFrame(cv::Mat& image)
{
    if (!frame->width || !frame->height) {
        return false;
    } else {
        cv::Size frameSize(frame->width, frame->height);
        image = cv::Mat(frameSize, CV_8UC1, frame->data[0]);
        return true;
    }
}


AVRational LibavReader::timeBase()
{
    if (idxVideoStream >= 0) {
        return (ic->streams[idxVideoStream]->time_base);
    } else {
        return AVRational {0,0};
    }
}


bool LibavReader::getVideoStreamInfo(VideoStream& videoStreamInfo)
{
    // reader opened
    if (idxVideoStream >= 0) {
        videoStreamInfo.frameRate = av_stream_get_r_frame_rate(ic->streams[idxVideoStream]);
        videoStreamInfo.timeBase = ic->streams[idxVideoStream]->time_base;
        videoStreamInfo.videoCodecParameters = ic->streams[idxVideoStream]->codecpar;
        return true;
    } else {
        avErrMsg("VideoReader must be opened in order to provide steam info");
        return false;
    }
}


/*** LibavWriter *************************************************************/

LibavWriter::LibavWriter() :
    idxVideoStream(-1),
    isOpen(false),
    oc(nullptr),
    os(nullptr),
    packetCount(0)
{

}


LibavWriter::~LibavWriter()
{
    close();
}


void freeOpenWriter(AVFormatContext *oc)
{
    avformat_free_context(oc);
}


void LibavWriter::close() {
    int ret = av_write_trailer(oc);
    if (ret < 0) {
        avErrMsg("Failed to write trailer", ret);
    }

    ret = avio_closep(&oc->pb);
    if (ret < 0) {
        avErrMsg("Failed to close output file", ret);
    }

    freeOpenWriter(oc);
    idxVideoStream = -1;
    isOpen = false;
}


bool LibavWriter::frameRate(AVRational fps)
{
    if (isOpen) {
        av_stream_set_r_frame_rate(os, fps);
        if (os->r_frame_rate.den == fps.den && os->r_frame_rate.num == fps.num) {
            return true;
        } else {
            avErrMsg("Failed to set frame rate");
            return false;
        }
    } else {
        avErrMsg("LibavWriter must be open in order to set frame rate");
        return false;
    }
}


int LibavWriter::init()
{
    av_register_all();
    return 0;
}


int LibavWriter::open(std::string file, AVCodecParameters* vCodecParams)
{
    int ret = avformat_alloc_output_context2(&oc, nullptr, nullptr, file.c_str());

    if (ret < 0) {
        avErrMsg("Failed to create output context", ret);
        freeOpenWriter(oc);
        return ret;
    }

    os = avformat_new_stream(oc, nullptr);
    if (!os) {
        avErrMsg("Failed to create output stream");
        freeOpenWriter(oc);
        return -1;
    }
    idxVideoStream = os->index;

    ret = avcodec_parameters_copy(os->codecpar, vCodecParams);
    if (ret < 0) {
        avErrMsg("Failed to set codec parameters", ret);
        freeOpenWriter(oc);
        return ret;
    }

    ret = avio_open(&oc->pb, file.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        avErrMsg("Failed to open output file", ret);
        freeOpenWriter(oc);
        return ret;
    }

    ret = avformat_write_header(oc, nullptr);
    if (ret < 0) {
        avErrMsg("Failed to write header", ret);
        avio_closep(&oc->pb);
        freeOpenWriter(oc);
        return ret;
    }

    packetCount = 0;
    isOpen = true;
    return 0;
}


bool LibavWriter::timeBase(AVRational timeBase)
{
    if (isOpen) {
        os->time_base = timeBase;
        return true;
    } else {
        avErrMsg("LibavWriter must be open in order to set time base");
        return false;
    }

}


bool LibavWriter::writeVideoPacket(AVPacket *packet)
{
    // ändern: video stream index, pos = -1
    packet->stream_index = idxVideoStream;
    packet->pos = -1;
    // calc pts: pts = packetCount * time base * (1 / fps)
    int64_t time_base = os->time_base.den * os->r_frame_rate.den / os->time_base.num / os->r_frame_rate.num;
    packet->pts = packet->dts = packetCount * time_base;

    int ret = av_interleaved_write_frame(oc, packet);
    if (ret < 0) {
        avErrMsg("Failed to write video packet", ret);
        return false;
    }

    ++packetCount;
    return true;
}
