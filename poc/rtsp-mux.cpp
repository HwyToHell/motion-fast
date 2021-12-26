#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

extern "C" {
//#include "libavcodec/avcodec.h"
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include "libavreadwrite.h"

// Some public stream. The code works with RTSP, RTMP, MJPEG, etc.
// static const char SOURCE_NAME[] = "http://81.83.10.9:8001/mjpg/video.mjpg"; // works!

// My goal was an actual cam streaming via HLS, but here are some random HLS streams
// that reproduce the problem quite well. Playlists may differ, but the error is exactly the same
//static const char SOURCE_NAME[] = "http://qthttp.apple.com.edgesuite.net/1010qwoeiuryfg/sl.m3u8"; // fails!
static const char SOURCE_NAME[] = "https://bitdash-a.akamaihd.net/content/sintel/hls/playlist.m3u8";
// static const char SOURCE_NAME[] = "https://bitdash-a.akamaihd.net/content/MI201109210084_1/m3u8s/f08e80da-bf1d-4e3d-8899-f0f6155f6efa.m3u8"; // fails!

using Pkt = std::unique_ptr<AVPacket, void (*)(AVPacket *)>;
std::deque<Pkt> frame_buffer;
std::mutex frame_mtx;
std::condition_variable frame_cv;
std::atomic_bool keep_running{true};

AVCodecParameters *common_codecpar = nullptr;
std::mutex codecpar_mtx;
std::condition_variable codecpar_cv;

static int readBuffer(void *opaque, uint8_t *buf, int buf_size)
{
    // This function must fill the buffer with data and return number of bytes copied.
    // opaque is the pointer to private_data in the call to avio_alloc_context (4th param)

    memcpy(buf, opaque, buf_size);
    return buf_size;
}


void read_frames_from_source(unsigned N)
{
    AVFormatContext *fmt_ctx = avformat_alloc_context();
    size_t bufSize = 4096;
    unsigned char *pBuffer = (unsigned char*)av_mallocz( bufSize );
    fmt_ctx->pb = avio_alloc_context(pBuffer, bufSize, 0, pBuffer,  readBuffer, NULL, NULL);

    AVProbeData probeData ={0};
    probeData.buf_size = 4096;
    probeData.filename = SOURCE_NAME;
    probeData.buf = (unsigned char*)av_mallocz(probeData.buf_size + AVPROBE_PADDING_SIZE);




    int err = avformat_open_input(&fmt_ctx, SOURCE_NAME, nullptr, nullptr);
    if (err < 0) {
        avErrMsg("cannot open input", err);
        avformat_free_context(fmt_ctx);
        return;
    }

    err = avformat_find_stream_info(fmt_ctx, nullptr);
    if (err < 0) {
        std::cerr << "cannot find stream info" << std::endl;
        avformat_free_context(fmt_ctx);
        return;
    }

    // Simply finding the first video stream, preferrably H.264. Others are ignored below
    int video_stream_id = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        auto *c = fmt_ctx->streams[i]->codecpar;
        if (c->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_id = i;
            if (c->codec_id == AV_CODEC_ID_H264)
                break;
        }
    }

    if (video_stream_id < 0) {
        std::cerr << "failed to find find video stream" << std::endl;
        avformat_free_context(fmt_ctx);
        return;
    }

    {   // Here we have the codec params and can launch the writer
        std::lock_guard<std::mutex> locker(codecpar_mtx);
        common_codecpar = fmt_ctx->streams[video_stream_id]->codecpar;
    }
    codecpar_cv.notify_all();

    unsigned cnt = 0;
    while (++cnt <= N) { // we read some limited number of frames
        Pkt pkt{av_packet_alloc(), [](AVPacket *p) { av_packet_free(&p); }};

        err = av_read_frame(fmt_ctx, pkt.get());
        if (err < 0) {
            std::cerr << "read packet error" << std::endl;
            continue;
        }

        // That's why the cycle above, we write only one video stream here
        if (pkt->stream_index != video_stream_id)
            continue;

        {
            std::lock_guard<std::mutex> locker(frame_mtx);
            frame_buffer.push_back(std::move(pkt));
        }
        frame_cv.notify_one();
    }

    keep_running.store(false);
    avformat_free_context(fmt_ctx);
}

void write_frames_into_file(std::string filepath)
{
    AVFormatContext *out_ctx = nullptr;
    int err = avformat_alloc_output_context2(&out_ctx, nullptr, "matroska", filepath.c_str());
    if (err < 0) {
        std::cerr << "avformat_alloc_output_context2 failed" << std::endl;
        return;
    }

    AVStream *video_stream = avformat_new_stream(out_ctx, avcodec_find_encoder(common_codecpar->codec_id)); // the proper way
    // AVStream *video_stream = avformat_new_stream(out_ctx, avcodec_find_encoder(AV_CODEC_ID_H264)); // forcing the H.264
    // ------>> HERE IS THE TROUBLE, NO CODEC WORKS WITH HLS <<------

    int video_stream_id = video_stream->index;

    err = avcodec_parameters_copy(video_stream->codecpar, common_codecpar);
    if (err < 0) {
        std::cerr << "avcodec_parameters_copy failed" << std::endl;
    }

    if (!(out_ctx->flags & AVFMT_NOFILE)) {
        err =  avio_open(&out_ctx->pb, filepath.c_str(), AVIO_FLAG_WRITE);
        if (err < 0) {
            std::cerr << "avio_open fail" << std::endl;
            return;
        }
    }

    err = avformat_write_header(out_ctx, nullptr); // <<--- ERROR WITH HLS HERE
    if (err < 0) {
        std::cerr << "avformat_write_header failed" << std::endl;
        return; // here we go with hls
    }

    unsigned cnt = 0;
    while (true) {
        std::unique_lock<std::mutex> locker(frame_mtx);
        frame_cv.wait(locker, [&] { return !frame_buffer.empty() || !keep_running; });

        if (!keep_running)
            break;

        Pkt pkt = std::move(frame_buffer.front());
        frame_buffer.pop_front();
        ++cnt;
        locker.unlock();

        pkt->stream_index = video_stream_id; // mandatory
        err = av_write_frame(out_ctx, pkt.get());
        if (err < 0) {
            std::cerr << "av_write_frame failed " << cnt << std::endl;
        } else if (cnt % 25 == 0) {
            std::cout << cnt << " OK" << std::endl;
        }
    }

    av_write_trailer(out_ctx);
    avformat_free_context(out_ctx);
}

int main()
{
    avformat_network_init();
    std::thread reader(std::bind(&read_frames_from_source, 1000));
    std::thread writer;

    // Writer wont start until reader's got AVCodecParameters
    // In this example it spares us from setting writer's params properly manually

    {   // Waiting for codec params to be set
        std::unique_lock<std::mutex> locker(codecpar_mtx);
        codecpar_cv.wait(locker, [&] { return common_codecpar != nullptr; });
        writer = std::thread(std::bind(&write_frames_into_file, "out.mkv"));
    }

    reader.join();
    keep_running.store(false);
    writer.join();

    return 0;
}
