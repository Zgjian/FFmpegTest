#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <memory>


#define INBUF_SIZE 4096


static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt, const char *filename)
{
    char buf[1024];
    int ret;

    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }

        printf("saving frame %3d\n", dec_ctx->frame_number);
        fflush(stdout);

        /* the picture is allocated by the decoder. no need to
        free it */
        //snprintf(buf, sizeof(buf), "%s-%d", filename, dec_ctx->frame_number);
        //pgm_save(frame->data[0], frame->linesize[0],
        //    frame->width, frame->height, buf);

        const int y_size = frame->width * frame->height;
        const int yuv_size = y_size * 3 / 2;
        std::shared_ptr<char> yuv_data(new char[yuv_size]);

        for (int i = 0; i < frame->height; i++)
            memcpy(yuv_data.get() + frame->width * i, frame->data[0] + frame->linesize[0] * i, frame->width);

        for (int j = 0; j < frame->height / 2; j++)
            memcpy(yuv_data.get() + frame->width / 2 * j + y_size, frame->data[1] + frame->linesize[1] * j, frame->width / 2);

        for (int k = 0; k < frame->height / 2; k++)
            memcpy(yuv_data.get() + frame->width / 2 * k + y_size * 5 / 4, frame->data[2] + frame->linesize[2] * k, frame->width / 2);


        FILE *fp_out = fopen(filename, "ab+");
        //Y, U, V   OK
        //for (int i = 0; i < frame->height; i++) {
        //    fwrite(frame->data[0] + frame->linesize[0] * i, 1, frame->width, fp_out);
        //}
        //for (int i = 0; i < frame->height / 2; i++) {
        //    fwrite(frame->data[1] + frame->linesize[1] * i, 1, frame->width / 2, fp_out);
        //}
        //for (int i = 0; i < frame->height / 2; i++) {
        //    fwrite(frame->data[2] + frame->linesize[2] * i, 1, frame->width / 2, fp_out);
        //}

        fwrite(yuv_data.get(), 1, yuv_size, fp_out);
        fclose(fp_out);
    }
}

int main(int argc, char **argv)
{
    const char *filename = "oceans.h264";
    const char *outfilename = "1.yuv";

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        exit(1);
    }

    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    /* find the video decoder */
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    AVCodecParserContext *parser = av_parser_init(codec->id);
    if (!parser) {
        fprintf(stderr, "parser not found\n");
        exit(1);
    }

    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    /* For some codecs, such as msmpeg4 and mpeg4, width and height
    MUST be initialized there because this information is not
    available in the bitstream. */

    /* open it */
    if (avcodec_open2(c, codec, nullptr) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    uint8_t *data;
    size_t  data_size;
    while (!feof(f)) {
        /* read raw data from the input file */
        data_size = fread(inbuf, 1, INBUF_SIZE, f);
        if (!data_size)
            break;

        /* use the parser to split the data into frames */
        data = inbuf;
        while (data_size > 0) {
            int ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size,
                data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (ret < 0) {
                fprintf(stderr, "Error while parsing\n");
                exit(1);
            }
            data += ret;
            data_size -= ret;

            if (pkt->size) {
                decode(c, frame, pkt, outfilename);
            }
        }
    }

    /* flush the decoder */
    decode(c, frame, nullptr, outfilename);

    fclose(f);

    av_parser_close(parser);
    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}