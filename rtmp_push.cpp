#include <stdbool.h>
extern "C" {
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif // !__STDC_CONSTANT_MACROS

#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}
#include <iostream>
#include <string>

using namespace std;


// ffmpeg -re -i tnhaoxc.flv -c copy -f flv rtmp://192.168.0.104/live
// ffmpeg -i rtmp://192.168.0.104/live -c copy tnlinyrx.flv
// ./streamer tnhaoxc.flv rtmp://192.168.0.104/live
// ./streamer rtmp://192.168.0.104/live tnhaoxc.flv
int main(int argc, char **argv)
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    const char *in_filename, *out_filename;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = NULL;
    int stream_mapping_size = 0;

    if (argc < 3) {
        printf("usage: %s input output\n"
            "API example program to remux a media file with libavformat and libavcodec.\n"
            "The output format is guessed according to the file extension.\n"
            "\n", argv[0]);
        //return 1;
    }

    in_filename = "oceans.h264";
    out_filename = "rtmp://127.0.0.1/live/2";

    // 1. ������
    // 1.1 ��ȡ�ļ�ͷ����ȡ��װ��ʽ�����Ϣ
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        printf("Could not open input file '%s'", in_filename);
        //goto end;
        return -1;
    }

    // 1.2 ����һ�����ݣ���ȡ�������Ϣ
    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        printf("Failed to retrieve input stream information");
        //goto end;
        return -1;
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    // 2. �����
    // 2.1 �������ctx
    bool push_stream = false;
    string ofmt_name;
    if (strstr(out_filename, "rtmp://") != NULL) {
        push_stream = true;
        ofmt_name = "flv";
    }
    else if (strstr(out_filename, "udp://") != NULL) {
        push_stream = true;
        ofmt_name = "mpegts";
    }
    else {
        push_stream = false;
        ofmt_name = "";
    }
    avformat_alloc_output_context2(&ofmt_ctx, NULL, ofmt_name.c_str(), out_filename);
    if (!ofmt_ctx) {
        printf("Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    stream_mapping_size = ifmt_ctx->nb_streams;
    stream_mapping = (int*)av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

    AVRational frame_rate;
    double duration;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }

        if (push_stream && (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO)) {
            frame_rate = av_guess_frame_rate(ifmt_ctx, in_stream, NULL);
            duration = (frame_rate.num && frame_rate.den ? av_q2d(AVRational{ frame_rate.den, frame_rate.num }) : 0);
        }

        stream_mapping[i] = stream_index++;

        // 2.2 ��һ������(out_stream)��ӵ�����ļ�(ofmt_ctx)
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            printf("Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        // 2.3 ����ǰ�������еĲ����������������
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            printf("Failed to copy codec parameters\n");
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {    // TODO: �о�AVFMT_NOFILE��־
        // 2.4 ��������ʼ��һ��AVIOContext�����Է���URL(out_filename)ָ������Դ
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            printf("Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    // 3. ���ݴ���
    // 3.1 д����ļ�ͷ
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        printf("Error occurred when opening output file\n");
        goto end;
    }

    while (1) {
        AVStream *in_stream, *out_stream;

        // 3.2 ���������ȡһ��packet
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0) {
            break;
        }

        in_stream = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }

        int codec_type = in_stream->codecpar->codec_type;
        if (push_stream && (codec_type == AVMEDIA_TYPE_VIDEO)) {
            av_usleep((int64_t)(duration*AV_TIME_BASE));
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];

        /* copy packet */
        // 3.3 ����packet�е�pts��dts
        // ����AVStream.time_base(�����е�time_base)��˵����
        // ���룺�������к���time_base����avformat_find_stream_info()�п�ȡ��ÿ�����е�time_base
        // �����avformat_write_header()���������ķ�װ��ʽȷ��ÿ������time_base��д���ļ���
        // AVPacket.pts��AVPacket.dts�ĵ�λ��AVStream.time_base����ͬ�ķ�װ��ʽAVStream.time_base��ͬ
        // ��������ļ��У�ÿ��packet��Ҫ���������װ��ʽ���¼���pts��dts
        av_packet_rescale_ts(&pkt, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        //FILE *fp = fopen("111111.h264", "ab+");
        //fwrite(pkt.data, pkt.size, 1, fp);
        //fclose(fp);

        static int i = 0;
        pkt.pts = i++;

        printf("pts: %d\n", i);

        // 3.4 ��packetд�����
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0) {
            printf("Error muxing packet\n");
            break;
        }
        av_packet_unref(&pkt);
    }

    // 3.5 д����ļ�β
    av_write_trailer(ofmt_ctx);

end:
    avformat_close_input(&ifmt_ctx);

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE)) {
        avio_closep(&ofmt_ctx->pb);
    }
    avformat_free_context(ofmt_ctx);

    av_freep(&stream_mapping);

    if (ret < 0 && ret != AVERROR_EOF) {
        //printf("Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}