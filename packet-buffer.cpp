extern "C" {
#include <libavformat/avformat.h>
}
#include <deque>
#include <iostream>

bool waitForEnter() {
    using namespace std;
    cout << endl << "Press <enter> to continue" << endl;
    string str;
    getline(cin, str);
    return true;
}


int main_packet(int argc, const char *argv[])
{
    AVFormatContext *input_format_context = nullptr, *output_format_context = nullptr;
    AVPacket packet;
    const char *in_filename, *out_filename;
    int ret;
    int stream_index = 0;
    int *streams_list = nullptr;
    unsigned int number_of_streams = 0;
    int fragmented_mp4_options = 0;
    AVDictionary* opts = nullptr;

    std::deque<AVPacket*> buffer;
    int bufSize = 100;
    int packetCount = 0;

    if (argc < 3) {
      printf("You need to pass at least two parameters.\n");
      return -1;
    } else if (argc == 4) {
      fragmented_mp4_options = 1;
    }

    av_register_all();
    in_filename  = argv[1];
    out_filename = argv[2];

    if ((ret = avformat_open_input(&input_format_context, in_filename, nullptr, nullptr)) < 0) {
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
      fprintf(stderr, "Could not create output context\n");
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
      AVCodecParameters *in_codecpar = in_stream->codecpar;
      if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
          in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
          in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        streams_list[i] = -1;
        continue;
      }
      streams_list[i] = stream_index++;
      out_stream = avformat_new_stream(output_format_context, nullptr);
      if (!out_stream) {
        fprintf(stderr, "Failed allocating output stream\n");
        ret = AVERROR_UNKNOWN;
        goto end;
      }
      ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
      if (ret < 0) {
        fprintf(stderr, "Failed to copy codec parameters\n");
        goto end;
      }
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


    /* buffering AVPackets */
    AVStream *in_stream, *out_stream;
    AVPacket *cPacket;
    //cPacket = av_packet_alloc();

    // store packets in buffer
    for (int i = 0; i < bufSize; ++i) {
        ret = av_read_frame(input_format_context, &packet);
        if (ret < 0)
          break;
        cPacket = av_packet_clone(&packet);
        buffer.push_back(cPacket);
        printf("address %i: %p\n", i, static_cast<void*>(cPacket));
        av_packet_unref(&packet);
        // waitForEnter();
        // exit(-1);
    }

    // write file header
    ret = avformat_write_header(output_format_context, &opts);
    if (ret < 0) {
      fprintf(stderr, "Error occurred when opening output file\n");
      goto end;
    }

    // remove first key frame
    //buffer.pop_front();

    // remove packets and write to file
    while (!buffer.empty()) {
        AVPacket *qPacket = buffer.front();

        // locate stream
        in_stream = input_format_context->streams[qPacket->stream_index];
        qPacket->stream_index = streams_list[packet.stream_index];
        out_stream = output_format_context->streams[qPacket->stream_index];

        // rescale presentation time
        qPacket->pts = av_rescale_q_rnd(qPacket->pts, in_stream->time_base, out_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        qPacket->dts = av_rescale_q_rnd(qPacket->dts, in_stream->time_base, out_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        qPacket->duration = av_rescale_q(qPacket->duration, in_stream->time_base, out_stream->time_base);
        qPacket->pos = -1; // unknown

        // write packet
        ret = av_interleaved_write_frame(output_format_context, qPacket);
        if (ret < 0) {
          fprintf(stderr, "Error muxing packet\n");
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
        avformat_close_input(&input_format_context);
        /* close output */
        if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
            avio_closep(&output_format_context->pb);
        avformat_free_context(output_format_context);
        av_freep(&streams_list);
        if (ret < 0 && ret != AVERROR_EOF) {
            char errbuf[1024] = { 0 };
            av_strerror(ret, errbuf, sizeof (errbuf));
            fprintf(stderr, "Error occurred: %s\n", errbuf);
        return 1;
      }

    return 0;
}
