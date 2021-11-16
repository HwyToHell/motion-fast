extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <opencv2/opencv.hpp>

#include <chrono>
#include <deque>
#include <iostream>
#include <numeric> // accumulate

#include <stdio.h>

typedef std::chrono::time_point<std::chrono::system_clock> TimePoint;

static void logging(const char *fmt, ...);


int decode(AVPacket *packet, AVCodecContext *codecCtx, AVFrame *frame) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    size_t errbufSize = AV_ERROR_MAX_STRING_SIZE;

    int ret = avcodec_send_packet(codecCtx, packet);
    if (ret < 0) {
        logging("Error sending packet to decoder: %s", av_make_error_string(errbuf, errbufSize, ret));
        return ret;
    }

    ret = avcodec_receive_frame(codecCtx, frame);
    /*
    if (ret >= 0) {
        logging(
            "Frame %d (type=%c, size=%d bytes, format=%d) pts %d key_frame %d [DTS %d]",
            codecCtx->frame_number,
            av_get_picture_type_char(frame->pict_type),
            frame->pkt_size,
            frame->format,
            frame->pts,
            frame->key_frame,
            frame->coded_picture_number
        );
    } else {
        logging("Error receiving frame from the decoder: %s", av_make_error_string(errbuf, errbufSize, ret));
    }*/

    return ret;
}


// suppress warning: vfprintf format string is not a string literal
__attribute__((__format__ (__printf__, 1, 0)))
static void logging(const char *fmt, ...)
{
    va_list args;
    fprintf( stderr, "LOG: " );
    va_start( args, fmt );
    vfprintf( stderr, fmt, args );
    va_end( args );
    fprintf( stderr, "\n" );
}


void statistics(std::vector<long long> samples, std::string name)
{
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wall"
    // std::vector<long long>::iterator itMax = std::max_element(samples.begin(), samples.end());
    long long max = *(std::max_element(samples.begin(), samples.end()));
    // long long max = *itMax;
    #pragma GCC diagnostic pop

    long long sum = std::accumulate(samples.begin(), samples.end(), 0);
    double mean = static_cast<double>(sum) / samples.size();

    double variance = 0, stdDeviation = 0;
    for (auto sample : samples) {
        variance += pow ((sample - mean), 2);
    }
    stdDeviation = sqrt(variance / samples.size());

    std::cout << std::endl << "===================================" << std::endl << name << std::endl;
    std::cout << "mean: " << mean << " max: " << max << std::endl;
    std::cout << "95%:  " << mean + (2 * stdDeviation) << std::endl;
}


bool waitForEnter() {
    using namespace std;
    cout << endl << "Press <enter> to continue" << endl;
    string str;
    getline(cin, str);
    return true;
}



int main_decode_buf(int argc, const char *argv[])
{
    AVFormatContext *input_format_context = nullptr, *output_format_context = nullptr;
    AVPacket *pPacket = nullptr;
    AVFrame *pFrame = nullptr;
    const char *in_filename, *out_filename;
    int ret;
    int stream_index = 0;
    int video_stream_index = -1;
    int *streams_list = nullptr;
    unsigned int number_of_streams = 0;
    int fragmented_mp4_options = 0;
    AVDictionary* opts = nullptr;
    AVCodecContext *pCodecCtx = nullptr;
    AVCodecParameters *pCodecParams = nullptr;
    AVCodec *pCodec = nullptr;

    std::deque<AVPacket*> buffer;
    int bufSize = 100;
    int packetCount = 0;

    // performance measurement
    TimePoint start ;
    TimePoint read;
    TimePoint push;
    TimePoint decodeTp;
    std::vector<long long> readTimes;
    std::vector<long long> pushTimes;
    std::vector<long long> decodeTimes;

    if (argc < 3) {
        printf("You need to pass at least two parameters.\n");
        return -1;
    } else if (argc == 4) {
        fragmented_mp4_options = 1;
    }

    av_register_all();
    in_filename  = argv[1];
    out_filename = argv[2];

    if (avformat_open_input(&input_format_context, in_filename, nullptr, nullptr) < 0) {
        fprintf(stderr, "Could not open input file '%s'", in_filename);
        goto end;
    }
    if ((ret = avformat_find_stream_info(input_format_context, nullptr)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }
    av_dump_format(input_format_context, 0, in_filename, 0);

    avformat_alloc_output_context2(&output_format_context, nullptr, nullptr, out_filename);
    if (!output_format_context) {
      logging("Could not create output context");
      ret = AVERROR_UNKNOWN;
      goto end;
    }
    number_of_streams = input_format_context->nb_streams;
    streams_list = static_cast<int*>(av_mallocz_array(number_of_streams, sizeof(*streams_list)));
    if (!streams_list) {
      ret = AVERROR(ENOMEM);
      goto end;
    }

    for (unsigned int i = 0; i < input_format_context->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = input_format_context->streams[i];

        logging("AVStream->time_base before open coded %d/%d", in_stream->time_base.num, in_stream->time_base.den);
        logging("AVStream->r_frame_rate before open coded %d/%d", in_stream->r_frame_rate.num, in_stream->r_frame_rate.den);
        logging("AVStream->start_time %" PRId64, in_stream->start_time);
        logging("AVStream->duration %" PRId64, in_stream->duration);

        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            streams_list[i] = -1;
            logging("Stream does not contain audio, video or subtitles");
        continue;
        }

        // find codec
        logging("Find decoder for video stream");
        if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            pCodecParams = in_codecpar;
            pCodec = avcodec_find_decoder(pCodecParams->codec_id);
            if (!pCodec) {
                logging("ERROR unsupported codec");
                return -1;
            }
            video_stream_index = static_cast<int>(i);
            logging("Codec: %s ID: %i sample rate: %lli resolution: %ix%i", pCodec->name, pCodec->id,
                    pCodecParams->sample_rate, pCodecParams->width, pCodecParams->height);
        }

        streams_list[i] = stream_index++;
        out_stream = avformat_new_stream(output_format_context, nullptr);
        if (!out_stream) {
            logging("Failed allocating output stream");
            ret = AVERROR_UNKNOWN;
        goto end;
        }
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            logging("Failed to copy codec parameters");
            goto end;
        }
    }

    if (video_stream_index == -1) {
      logging("File %s does not contain a video stream", argv[1]);
      return -1;
    }

    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (!pCodecCtx) {
        logging("Failed to allocate memory for AVCodecContext");
        return -1;
    }

    if (avcodec_parameters_to_context(pCodecCtx, pCodecParams) < 0) {
        logging("Failed to copy codec params to codec context");
        return -1;
    }

    if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
        logging("Failed to open codec");
        return -1;
    }


    // https://ffmpeg.org/doxygen/trunk/group__lavf__misc.html#gae2645941f2dc779c307eb6314fd39f10
    av_dump_format(output_format_context, 0, out_filename, 1);

    // unless it's a no file (we'll talk later about that) write to the disk (FLAG_WRITE)
    // but basically it's a way to save the file to a buffer so you can store it
    // wherever you want.
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
      ret = avio_open(&output_format_context->pb, out_filename, AVIO_FLAG_WRITE);
      if (ret < 0) {
        fprintf(stderr, "Could not open output file '%s'", out_filename);
        goto end;
      }
    }


    if (fragmented_mp4_options) {
      // https://developer.mozilla.org/en-US/docs/Web/API/Media_Source_Extensions_API/Transcoding_assets_for_MSE
      av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    }



    pPacket = av_packet_alloc();
    if (!pPacket) {
        logging("Failed to allocate memory for AVPacket");
        return -1;
    }

    pFrame = av_frame_alloc();
    if (!pFrame) {
        logging("Failed to allocate memory for AVFrame");
        return -1;
    }

    AVStream *in_stream, *out_stream;
    AVPacket *cPacket;
    //cPacket = av_packet_alloc();

    /* buffering AVPackets */
    // store packets in buffer
    for (int i = 0; i < bufSize; ++i) {
        // measurement
        // start = std::chrono::system_clock::now();

        ret = av_read_frame(input_format_context, pPacket);
        if (ret < 0) {
            logging("Error reading packet");
            break;
        }

        // decode video stream only
        if (pPacket->stream_index == video_stream_index) {
            read = std::chrono::system_clock::now();

            cPacket = av_packet_clone(pPacket);
            buffer.push_back(cPacket);
            printf("address %i: %p\n", i, static_cast<void*>(cPacket));
            push = std::chrono::system_clock::now();

            ret = decode(pPacket, pCodecCtx, pFrame);
            if (ret == 0) {
                /*
                for (size_t i = 0; i < 3; ++i) {
                    printf("linesize %lu: %i\n", i, pFrame->linesize[i]);
                }
                */


                cv::Size frameSize(pFrame->width, pFrame->height);
                cv::Mat img(frameSize, CV_8UC1, pFrame->data[0]);
                decodeTp = std::chrono::system_clock::now();

                cv::imshow("frame", img);
                if (cv::waitKey(40) == 32) 	{
                    std::cout << "SPACE pressed -> continue by hitting SPACE again, ESC to abort processing" << std::endl;
                    int key = cv::waitKey(0);
                    if (key == 27)
                        break;
                }


            } else if (ret != AVERROR(EAGAIN)) {
                logging("Decode error");
                break;
            }

            av_packet_unref(pPacket);
        }

        //long long readDuration = std::chrono::duration_cast<std::chrono::milliseconds>(read - start).count();
        //readTimes.push_back(readDuration);


        long long pushDuration = std::chrono::duration_cast<std::chrono::milliseconds>(push - read).count();
        pushTimes.push_back(pushDuration);

        long long decodeDuration = std::chrono::duration_cast<std::chrono::milliseconds>(decodeTp - push).count();
        decodeTimes.push_back(decodeDuration);


    } // end_for

    // write file header
    ret = avformat_write_header(output_format_context, &opts);
    if (ret < 0) {
      logging("Error occurred when opening output file");
      goto end;
    }

    // remove first key frame
    //buffer.pop_front();

    // remove packets and write to file
    while (!buffer.empty()) {
        AVPacket *qPacket = buffer.front();

        // locate stream
        in_stream = input_format_context->streams[qPacket->stream_index];
        qPacket->stream_index = streams_list[qPacket->stream_index];
        out_stream = output_format_context->streams[qPacket->stream_index];

        // rescale presentation time
        qPacket->pts = av_rescale_q_rnd(qPacket->pts, in_stream->time_base, out_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        qPacket->dts = av_rescale_q_rnd(qPacket->dts, in_stream->time_base, out_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        qPacket->duration = av_rescale_q(qPacket->duration, in_stream->time_base, out_stream->time_base);
        qPacket->pos = -1; // unknown

        // write packet
        ret = av_interleaved_write_frame(output_format_context, qPacket);
        if (ret < 0) {
          logging("Error muxing packet");
          av_packet_free(&qPacket);
          break;
        }
        //av_packet_unref(qPacket);
        av_packet_free(&qPacket);
        buffer.pop_front();
        printf("packet#: %i\n", ++packetCount);
    }


    /*
    while (1) {
      AVStream *in_stream, *out_stream;
      ret = av_read_frame(input_format_context, &packet);
      if (ret < 0)
        break;
      in_stream  = input_format_context->streams[packet.stream_index];
      if (packet.stream_index >= static_cast<int>(number_of_streams) || streams_list[packet.stream_index] < 0) {
        av_packet_unref(&packet);
        continue;
      }
      packet.stream_index = streams_list[packet.stream_index];
      out_stream = output_format_context->streams[packet.stream_index];
      // copy packet

      packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
      packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
      packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
      // https://ffmpeg.org/doxygen/trunk/structAVPacket.html#ab5793d8195cf4789dfb3913b7a693903
      packet.pos = -1;

      //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga37352ed2c63493c38219d935e71db6c1
      ret = av_interleaved_write_frame(output_format_context, &packet);
      if (ret < 0) {
        fprintf(stderr, "Error muxing packet\n");
        break;
      }
      av_packet_unref(&packet);
    }
    */

    //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga7f14007e7dc8f481f054b21614dfec13
    av_write_trailer(output_format_context);

    end:
        av_frame_free(&pFrame);
        av_packet_free(&pPacket);
        avformat_close_input(&input_format_context);
        /* close output */
        if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
            avio_closep(&output_format_context->pb);
        avformat_free_context(output_format_context);
        av_freep(&streams_list);
        if (ret < 0 && ret != AVERROR_EOF) {
            char errbuf[1024] = { 0 };
            av_strerror(ret, errbuf, sizeof (errbuf));
            logging("Error when finishing program: %s", errbuf);
            return 1;
        }

        //statistics(readTimes, "read_packet in ms");
        statistics(pushTimes, "push_packet in ms");
        statistics(decodeTimes, "decode_frame in ms");


    return 0;
}
