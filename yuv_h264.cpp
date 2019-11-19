#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}
#include <memory>


#define INBUF_SIZE 4096

static AVCodecContext *dec_ctx;
static AVCodecParserContext *parser;
static AVCodecContext *enc_ctx;
static AVPacket *enc_pkt;

static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3" PRId64 "\n", frame->pts);

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }

        printf("Write packet %3" PRId64 " (size=%5d)\n", pkt->pts, pkt->size);

        FILE *fp = fopen("1111.h264", "ab+");
        fwrite(pkt->data, pkt->size, 1, fp);
        fclose(fp);

        av_packet_unref(pkt);
    }
}


static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt, const char *filename)
{
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


        //FILE *fp_out = fopen(filename, "ab+");
        //fwrite(yuv_data.get(), yuv_size, 1, fp_out);
        //fclose(fp_out);


        // Zgj++
        static int i = 0;
        frame->pts = i++;


        // encode
        encode(enc_ctx, frame, enc_pkt);
    }
}

int InitDecoder()
{
    const AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!decoder) {
        fprintf(stderr, "Codec not found decoder\n");
        return -1;
    }

    parser = av_parser_init(decoder->id);
    if (!parser) {
        fprintf(stderr, "parser not found\n");
        return -1;
    }

    dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) {
        fprintf(stderr, "Could not allocate video decoder context\n");
        return -1;
    }

    /* open it */
    if (avcodec_open2(dec_ctx, decoder, nullptr) < 0) {
        fprintf(stderr, "Could not open decoder\n");
        return -1;
    }

    return 0;
}

int InitEncoder(int width, int height)
{
    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!encoder) {
        fprintf(stderr, "Encoder not found\n");
        return -1;
    }

    enc_ctx = avcodec_alloc_context3(encoder);
    if (!enc_ctx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    enc_pkt = av_packet_alloc();
    if (!enc_pkt)
        return -1;

    /* put sample parameters */
    enc_ctx->bit_rate = 400000;
    /* resolution must be a multiple of two */
    enc_ctx->width = width;
    enc_ctx->height = height;
    /* frames per second */
    enc_ctx->time_base = AVRational{ 1, 25 };
    enc_ctx->framerate = AVRational{ 25, 1 };

    enc_ctx->gop_size = 10;
    enc_ctx->max_b_frames = 1;
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    av_opt_set(enc_ctx->priv_data, "preset", "placebo", 0);

    int ret = avcodec_open2(enc_ctx, encoder, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open encoder\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *filename = "oceans.h264";
    const char *outfilename = "1.yuv";

    AVPacket *dec_pkt = av_packet_alloc();
    if (!dec_pkt) {
        exit(1);
    }

    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    int ret = InitDecoder();
    if (ret != 0) {
        return -1;
    }

    ret = InitEncoder(960, 400);
    if (ret != 0) {
        return -1;
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
            int ret = av_parser_parse2(parser, dec_ctx, &dec_pkt->data, &dec_pkt->size,
                data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (ret < 0) {
                fprintf(stderr, "Error while parsing\n");
                exit(1);
            }
            data += ret;
            data_size -= ret;

            if (dec_pkt->size)
                decode(dec_ctx, frame, dec_pkt, outfilename);
        }
    }

    /* flush the decoder */
    decode(dec_ctx, frame, nullptr, outfilename);

    fclose(f);

    av_parser_close(parser);
    avcodec_free_context(&dec_ctx);
    av_frame_free(&frame);
    av_packet_free(&dec_pkt);

    return 0;
}