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


double LibavDecoder::frameTime(AVRational timeBase)
{
    if (m_frame) {
        return (static_cast<double>(m_frame->pts) * timeBase.num / timeBase.den);
    } else {
        return 0;
    }
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
    m_inCtx(nullptr),
    m_idxVideoStream(-1),
    m_packet(nullptr)
{

}


LibavReader::~LibavReader()
{
    close();
}


void freeOpenReader(AVFormatContext *ic) {
    avformat_close_input(&ic);
    avformat_free_context(ic);
}


AVPacket* LibavReader::cloneVideoPacket()
{
    return av_packet_clone(m_packet);
}


void LibavReader::close()
{
    av_packet_free(&m_packet);
    freeOpenReader(m_inCtx);
    m_idxVideoStream = -1;
}


bool LibavReader::getVideoStreamInfo(VideoStream& videoStreamInfo)
{
    // reader opened
    if (m_idxVideoStream >= 0) {
        videoStreamInfo.frameRate = av_stream_get_r_frame_rate(m_inCtx->streams[m_idxVideoStream]);
        videoStreamInfo.timeBase = m_inCtx->streams[m_idxVideoStream]->time_base;
        videoStreamInfo.videoCodecParameters = m_inCtx->streams[m_idxVideoStream]->codecpar;
        return true;
    } else {
        avErrMsg("VideoReader must be opened in order to provide steam info");
        return false;
    }
}


int LibavReader::init()
{
    av_register_all();

    return 0;
}


bool LibavReader::isOpen()
{
    return (m_idxVideoStream >=0) ? true : false;
}


int LibavReader::open(std::string fileName)
{
    int ret = -1;

    // dict for setting RTSP timeout
    AVDictionary* opts = nullptr;
    // set timeout in microseconds -> 5 sec
    av_dict_set(&opts, "stimeout", "5000000", 0);
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);

    //AVDictionaryEntry *optTimeout = av_dict_get(opts, "stimeout", nullptr, 0);
    //AVDictionaryEntry *optTransport = av_dict_get(opts, "rtsp_transport", nullptr, 0);

    m_inCtx = avformat_alloc_context();

    // TODO set AVDict for timeout
    // https://stackoverflow.com/questions/34034125/i-dont-know-the-time-unit-to-use-for-av-dict-set-to-set-a-timeout
    // ret = avformat_open_input(&m_inCtx, fileName.c_str(), nullptr, nullptr);
    ret = avformat_open_input(&m_inCtx, fileName.c_str(), nullptr, &opts);
    if (ret < 0) {
        avErrMsg("Failed to open input", ret);
        freeOpenReader(m_inCtx);
        // std::cout << "Error number open input: " << ret << std::endl;
        return ret;
    }

    ret = avformat_find_stream_info(m_inCtx, nullptr);
    if (ret < 0) {
        avErrMsg("Failed to retrieve input stream information", ret);
        freeOpenReader(m_inCtx);
        return ret;
    }

    // get video stream and decoder
    m_idxVideoStream = -1;
    for (unsigned int i = 0; i < m_inCtx->nb_streams; ++i) {
        AVStream *inStream = m_inCtx->streams[i];
        AVCodecParameters *inCodecPar = inStream->codecpar;
        if (inCodecPar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_idxVideoStream = static_cast<int>(i);
            AVCodec* codec = avcodec_find_decoder(inCodecPar->codec_id);
            if (!codec) {
                avErrMsg("Unsupported codec for video stream");
                m_idxVideoStream = -2;
            }
        }
    }
    if (m_idxVideoStream < 0) {
        avErrMsg("Video input does not contain a video stream or unsupported codec");
        freeOpenReader(m_inCtx);
        return ret;
    }

    // alloc packet struct
    m_packet = av_packet_alloc();
    if (!m_packet) {
        avErrMsg("Failed to allocate memory for AVPacket");
        return -1;
    }

    return 0;
}


bool LibavReader::playStream()
{
    // reader opened
    if (m_idxVideoStream >= 0) {
        av_read_play(m_inCtx);
        return true;
    } else {
        avErrMsg("VideoReader must be opened in order to play stream");
        return false;
    }

}


bool LibavReader::readVideoPacket(AVPacket*& pkt)
{
    pkt = nullptr;
    int ret = -1;
    // read videostream only
    for (unsigned int i = 0; i < m_inCtx->nb_streams; i++) {
        ret = av_read_frame(m_inCtx, m_packet);
        if (ret == AVERROR_EOF) {
            std::cout << "End of file or stream" << std::endl;
            return false;
        } else if (ret < 0) {
            avErrMsg("Failed to read packet", ret);
            return false;
        }

        if (m_packet->stream_index == m_idxVideoStream) {
            pkt = av_packet_clone(m_packet);
            av_packet_unref(m_packet);
            return true;
        }
    }
    return false;
}



/*** LibavWriter *************************************************************/

LibavWriter::LibavWriter() :
    m_idxVideoStream(-1),
    m_isOpen(false),
    m_outCtx(nullptr),
    m_outStream(nullptr),
    m_packetCount(0)
{

}


LibavWriter::~LibavWriter()
{
    if (m_isOpen)
        close();
}


void freeOpenWriter(AVFormatContext *m_outCtx)
{
    avformat_free_context(m_outCtx);
}


void LibavWriter::close() {
    int ret = av_write_trailer(m_outCtx);
    if (ret < 0) {
        avErrMsg("Failed to write trailer", ret);
    }

    ret = avio_closep(&m_outCtx->pb);
    if (ret < 0) {
        avErrMsg("Failed to close output file", ret);
    }

    freeOpenWriter(m_outCtx);
    m_idxVideoStream = -1;
    m_isOpen = false;
}


int LibavWriter::init()
{
    av_register_all();
    return 0;
}

bool LibavWriter::isOpen()
{
    return m_isOpen;
}

int LibavWriter::open(std::string file, VideoStream vStreamInfo)
{
    int ret = avformat_alloc_output_context2(&m_outCtx, nullptr, nullptr, file.c_str());

    if (ret < 0) {
        avErrMsg("Failed to create output context", ret);
        freeOpenWriter(m_outCtx);
        return ret;
    }

    m_outStream = avformat_new_stream(m_outCtx, nullptr);
    if (!m_outStream) {
        avErrMsg("Failed to create output stream");
        freeOpenWriter(m_outCtx);
        return -1;
    }
    m_idxVideoStream = m_outStream->index;

    ret = avcodec_parameters_copy(m_outStream->codecpar, vStreamInfo.videoCodecParameters);
    if (ret < 0) {
        avErrMsg("Failed to set codec parameters", ret);
        freeOpenWriter(m_outCtx);
        return ret;
    }

    if (!setTimeBase(vStreamInfo.timeBase)) {
        avErrMsg("Failed to set time base");
        return -1;
    }
    if (!setFrameRate(vStreamInfo.frameRate)) {
        avErrMsg("Failed to set frame rate");
        return -1;
    }

    ret = avio_open(&m_outCtx->pb, file.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        avErrMsg("Failed to open output file", ret);
        freeOpenWriter(m_outCtx);
        return ret;
    }

    ret = avformat_write_header(m_outCtx, nullptr);
    if (ret < 0) {
        avErrMsg("Failed to write header", ret);
        avio_closep(&m_outCtx->pb);
        freeOpenWriter(m_outCtx);
        return ret;
    }

    m_packetCount = 0;
    m_isOpen = true;
    return 0;
}


bool LibavWriter::setFrameRate(AVRational fps)
{
    if (m_outStream) {
        av_stream_set_r_frame_rate(m_outStream, fps);
        if (m_outStream->r_frame_rate.den == fps.den && m_outStream->r_frame_rate.num == fps.num) {
            return true;
        } else {
            avErrMsg("Failed to set frame rate");
            return false;
        }
    } else {
        avErrMsg("Output stream must be open in order to set frame rate");
        return false;
    }
}


bool LibavWriter::setTimeBase(AVRational timeBase)
{
    if (m_outStream) {
        m_outStream->time_base = timeBase;
        return true;
    } else {
        avErrMsg("Output stream must be open in order to set time base");
        return false;
    }

}


bool LibavWriter::writeVideoPacket(AVPacket *packet)
{
    // ändern: video stream index, pos = -1
    packet->stream_index = m_idxVideoStream;
    packet->pos = -1;
    // calc pts: pts = m_packetCount * time base * (1 / fps)
    int64_t time_base = m_outStream->time_base.den * m_outStream->r_frame_rate.den / m_outStream->time_base.num / m_outStream->r_frame_rate.num;
    packet->pts = packet->dts = m_packetCount * time_base;

    int ret = av_interleaved_write_frame(m_outCtx, packet);
    if (ret < 0) {
        avErrMsg("Failed to write video packet", ret);
        return false;
    }

    ++m_packetCount;
    return true;
}
