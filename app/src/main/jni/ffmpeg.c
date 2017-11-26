#include <module_video_jnc_myffmpeg_FFmpegUtils.h>
#include <string.h>
#include "My_LOG.h"
#include <time.h>
#include <stdio.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include "libavutil/log.h"
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>

//Error:(5, 51) module_video_jnc_myffmpeg_FFmpegUtils.h: No such file or directory
JNIEXPORT jstring JNICALL Java_module_video_jnc_myffmpeg_FFmpegUtils_stringNative
        (JNIEnv *env, jclass clazz) {

    char info[10000] = {0};
    sprintf(info, "%s\n", avcodec_configuration());
    return (*env)->NewStringUTF(env, info);
}

/*
 * Class:     module_video_jnc_myffmpeg_FFmpegUtils
 * Method:    stringJni
 * Signature: ()Ljava/lang/String;
 */

JNIEXPORT jstring JNICALL Java_module_video_jnc_myffmpeg_FFmpegUtils_stringJni
        (JNIEnv *env, jclass clazz) {
    char info[10000] = {0};
    sprintf(info, "%s\n", avcodec_configuration());
    return (*env)->NewStringUTF(env, info);
}


//Output FFmpeg's av_log()  
void custom_log(void *ptr, int level, const char *fmt, va_list vl) {
    FILE *fp = fopen("/storage/emulated/0/av_log.txt", "a+");
    if (fp) {
        vfprintf(fp, fmt, vl);
        fflush(fp);
        fclose(fp);
    }
}

/*
 * Class:     module_video_jnc_myffmpeg_FFmpegUtils
 * Method:    decode
 * Signature: (Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
 * 解码
 */
JNIEXPORT jint JNICALL Java_module_video_jnc_myffmpeg_FFmpegUtils_decode
        (JNIEnv *env, jclass clazz, jstring input_jstr, jstring output_jstr) {

    /**
     * AVFormatContext主要存储视音频封装格式中包含的信息，
     * AVInputFormat存储输入视音频封装格式，
     * 每种视音频封装格式都对应一个AVInputFormat结构
     * */
    AVFormatContext *pFormatCtx;


    int i, videoindex;

    /**
     * 每个avStream存储一个视频/音频流的相关数据；每个avstream对应一个AVCodecContext，
     * 存储该视频/音频流使用解码方式相关数据；
     * 每个AVCodecContext中对应一个avcodec，包含该视频/音频对应的解码器。每种解码器都对应一个avcodec结构。
     */
    AVCodecContext *pCodecCtx;
    //存储编解码器的结构体
    AVCodec *pCodec;
    //解码后的数据AVFrame
    AVFrame *pFrame/*, *pFrameYUV*/;

    uint8_t *out_buffer;
    //解码前的数据
    AVPacket *packet;
    int y_size;
    int ret, got_picture;
    /**
     * 这个格式对应的是图像拉伸，像素格式的转换
     */
//    struct SwsContext *img_convert_ctx;
    FILE *fp_yuv;
    int frame_cnt;
    clock_t time_start, time_finish;
    double time_duration = 0.0;
    const char *input_str = NULL;
    const char *output_str = NULL;
    char info[1000] = {0};

    input_str = (*env)->GetStringUTFChars(env, input_jstr, NULL);

    output_str = (*env)->GetStringUTFChars(env, output_jstr, NULL);
    //FFmpeg av_log() callback
    av_log_set_callback(custom_log);
    LOGE("input %s , output %s ", input_str, output_str);
    /**
     * 初始化，avformat，然后还初始化混合器，分流器。
     */
    av_register_all();
    /**
     * 初始化全局的网络模块，可选的，但是推荐初始化，如果使用了网络协议，必须强制初始化
     */
    avformat_network_init();

    /**
     * AVFormatContext,分配
     */
    pFormatCtx = avformat_alloc_context();

    /**
     * 打开输入流，注意使用avformat_close_input关闭，
     * 注意传入的是pFormatCtx的地址(pFormatCtx本身也是一个指针变量)（需要用二级指针来保存），
     */
    if (avformat_open_input(&pFormatCtx, input_str, NULL, NULL) != 0) {
        LOGE("Couldn't open input stream.\n");
        return -1;
    }
    /**
     * 读取文件的信息，第二个参数是AVDictionary（dict.h）,不过推荐使用tree.h中的。因为这个太大效率会低下
     */
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("Couldn't find stream information.\n");
        return -1;
    }

    videoindex = -1;

    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        //获取video轨道
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoindex = i;
            break;
        }
    }


    if (videoindex == -1) {
        LOGE("Couldn't find a video stream.\n");
        return -1;
    }

    pCodecCtx = pFormatCtx->streams[videoindex]->codec;

    /**
     * Find a registered decoder with a matching codec ID.
     * 找到一个解码器，如果没有就返回NULL
     */
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

    if (pCodec == NULL) {
        LOGE("Couldn't find Codec.\n");
        return -1;
    }

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        LOGE("Couldn't open codec.\n");
        return -1;
    }

    /**
     * 为解码后存储数据的pframe分配空间
     */
    pFrame = av_frame_alloc();
    /**
     * 将MP4中帧格式转换成yuv，这个pFrameYUV就是用来存储pFrameYUV格式的
     */
//    pFrameYUV = av_frame_alloc();

    /**
     * 分配空间
     */
    out_buffer = (unsigned char *) av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
    /**
     * 我也不知道要干嘛，好像是转格式之前设置的一些。
     */
//    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
//                         AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
    /**
     * 为解码前申请空间
     */
    packet = (AVPacket *) av_malloc(sizeof(AVPacket));

    /**
     * 转格式
     */
//    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
//                                     pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P,
//                                     SWS_BICUBIC, NULL, NULL, NULL);


    LOGE("[Input     ]%s\n", input_str);
    LOGE("%s[Output    ]%s\n", info, output_str);
    LOGE("%s[Format    ]%s\n", info, pFormatCtx->iformat->name);
    LOGE("%s[Codec     ]%s\n", info, pCodecCtx->codec->name);
    LOGE("%s[Resolution]%dx%d\n", info, pCodecCtx->width, pCodecCtx->height);

    /**
     * 读写方式打开文件，不是追加
     */
    fp_yuv = fopen(output_str, "wb+");

    if (fp_yuv == NULL) {
        printf("Cannot open output file.\n");
        return -1;
    }

    frame_cnt = 0;
    time_start = clock();

    /**
     * 读取一帧
     */
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        if (packet->stream_index == videoindex) {
            /**
             * 解码
             */
            ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);

            if (ret < 0) {
                LOGE("Decode Error.\n");
                return -1;
            }
            /**
             * 如果拿到图像帧
             */
            if (got_picture) {
                int lenY = malloc_usable_size(pFrame->data[0]);
                int lenU = malloc_usable_size(pFrame->data[1]);
                int lenV = malloc_usable_size(pFrame->data[2]);
                LOGE("解码 %d , %d , %d y_size=%d " , lenY ,  lenU ,  lenV , y_size);
                /**
                 * 按yuv420方式写入文件中。
                 */
                y_size = pCodecCtx->width * pCodecCtx->height;
                fwrite(pFrame->data[0], 1, y_size, fp_yuv);    //Y
                fwrite(pFrame->data[1], 1, y_size / 4, fp_yuv);  //U
                fwrite(pFrame->data[2], 1, y_size / 4, fp_yuv);  //V
                //Output info
                char pictype_str[10] = {0};
                switch (pFrame->pict_type) {
                    case AV_PICTURE_TYPE_I:
                        LOGE("decode  %s I", pictype_str);
                        break;
                    case AV_PICTURE_TYPE_P:
                        LOGE("decode  %s P", pictype_str);
                        break;
                    case AV_PICTURE_TYPE_B:
                        LOGE("decode  %s B", pictype_str);
                        break;
                    default:
                        LOGE("decode  %s Other", pictype_str);
                        break;
                }
                LOGI("Frame Index: %5d. Type:%s", frame_cnt, pictype_str);
                frame_cnt++;
            }
        }
        av_free_packet(packet);
    }
    //flush decoder
    //FIX: Flush Frames remained in Codec
    /**
     * 清空decoder，如果有数据就写入，没有数据就退出。
     */
    while (1) {
        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
        if (ret < 0) {
            LOGE("ret < 0 break ");
            break;
        }

        if (!got_picture) {
            LOGE(" got_picture %d break ", got_picture);
            break;
        }

//        sws_scale(img_convert_ctx, (const uint8_t *const *) pFrame->data, pFrame->linesize, 0,
//                  pCodecCtx->height,
//                  pFrameYUV->data, pFrameYUV->linesize);
        int y_size = pCodecCtx->width * pCodecCtx->height;
        fwrite(pFrame->data[0], 1, y_size, fp_yuv);    //Y
        fwrite(pFrame->data[1], 1, y_size / 4, fp_yuv);  //U
        fwrite(pFrame->data[2], 1, y_size / 4, fp_yuv);  //V
        //Output info
        char pictype_str[10] = {0};
        switch (pFrame->pict_type) {
            case AV_PICTURE_TYPE_I:
                sprintf(pictype_str, "I");
                LOGE("I %s", pictype_str);
                break;
            case AV_PICTURE_TYPE_P:
                LOGE("P %s", pictype_str);
                break;
            case AV_PICTURE_TYPE_B:
                LOGE("B %s", pictype_str);
                break;
            default:
                LOGE("Other ");
                break;
        }
        LOGE("Frame Index: %5d. Type :%s ", frame_cnt, pictype_str);
        frame_cnt++;
    }

    time_finish = clock();
    time_duration = (double) (time_finish - time_start);

    LOGE("%s[Time      ]%fms\n", info, time_duration);
    LOGE("%s[Count     ]%d\n", info, frame_cnt);
//    sws_freeContext(img_convert_ctx);
    fclose(fp_yuv);

    av_frame_free(&pFrame);
//    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    (*env)->ReleaseStringUTFChars(env, input_jstr, input_str);
    (*env)->ReleaseStringUTFChars(env, output_jstr, output_str);
    return 0;
}

/*
* Class:     module_video_jnc_myffmpeg_FFmpegUtils
* Method:    stream
* Signature: (Ljava/lang/String;Ljava/lang/String;)I
* 推流
*/
JNIEXPORT jint JNICALL Java_module_video_jnc_myffmpeg_FFmpegUtils_stream
        (JNIEnv *env, jclass clazz, jstring input_jstr, jstring output_jstr) {

    AVOutputFormat *ofmt = NULL;
    /**
       * AVFormatContext主要存储视音频封装格式中包含的信息，
       * AVInputFormat存储输入视音频封装格式，
       * 每种视音频封装格式都对应一个AVInputFormat结构
       * */
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    //解码前用来保存数据的
    AVPacket pkt;

    int ret, i;
    const char *input_str = NULL;
    const char *output_str = NULL;
    char info[1000] = {0};

    input_str = (*env)->GetStringUTFChars(env, input_jstr, NULL);
    output_str = (*env)->GetStringUTFChars(env, output_jstr, NULL);

    LOGE(" input str %s , output str %s ", input_str, output_str);

    //设置日志
    av_log_set_callback(custom_log);
    //初始化全部
    av_register_all();
    //网络初始化，要推流必须初始化
    avformat_network_init();
    //打开输入文件，从sdcard上读取视频
    if ((ret = avformat_open_input(&ifmt_ctx, input_str, 0, 0)) < 0) {
        LOGE("Could not open input file.");
        goto end;
    }
    //获取输入流
    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        LOGE("Failed to retrieve input stream information");
        goto end;
    }

    int videoindex = -1;
    /**
     * ifmt_ctx->nb_streams
     * 一个AVFormatContext可能有很多个stream，（视频，音频，字幕等。）
     */
    for (i = 0; i < ifmt_ctx->nb_streams; i++)
        if (ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoindex = i;
            break;
        }
    /**
     * 为ofmt_ctx分配空间
     */
    avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", output_str); //RTMP
    //avformat_alloc_output_context2(&ofmt_ctx, NULL, "mpegts", output_str);//UDP

    if (!ofmt_ctx) {
        LOGE("Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    ofmt = ofmt_ctx->oformat;
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        //Create output AVStream according to input AVStream
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
        if (!out_stream) {
            LOGE("Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        //Copy the settings of AVCodecContext
        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
        if (ret < 0) {
            LOGE("Failed to copy context from input to output stream codec context\n");
            goto end;
        }
        out_stream->codec->codec_tag = 0;
        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    //Open output URL
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, output_str, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOGE("Could not open output URL '%s'", output_str);
            goto end;
        }
    }
    //Write file header
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        LOGE("Error occurred when opening output URL\n");
        goto end;
    }

    int frame_index = 0;

    int64_t start_time = av_gettime();
    while (1) {
        AVStream *in_stream, *out_stream;
        //Get an AVPacket
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;
        //FIX：No PTS (Example: Raw H.264)
        //Simple Write PTS
        if (pkt.pts == AV_NOPTS_VALUE) {
            //Write PTS
            AVRational time_base1 = ifmt_ctx->streams[videoindex]->time_base;
            //Duration between 2 frames (us)
            int64_t calc_duration =
                    (double) AV_TIME_BASE / av_q2d(ifmt_ctx->streams[videoindex]->r_frame_rate);
            //Parameters
            pkt.pts = (double) (frame_index * calc_duration) /
                      (double) (av_q2d(time_base1) * AV_TIME_BASE);
            pkt.dts = pkt.pts;
            pkt.duration = (double) calc_duration / (double) (av_q2d(time_base1) * AV_TIME_BASE);
        }
        //Important:Delay
        if (pkt.stream_index == videoindex) {
            AVRational time_base = ifmt_ctx->streams[videoindex]->time_base;
            AVRational time_base_q = {1, AV_TIME_BASE};
            int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
            int64_t now_time = av_gettime() - start_time;
            if (pts_time > now_time)
                av_usleep(pts_time - now_time);
        }
        in_stream = ifmt_ctx->streams[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        /* copy packet */
        //Convert PTS/DTS
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        //Print to Screen
        if (pkt.stream_index == videoindex) {
            LOGE("Send %8d video frames to output URL\n", frame_index);
            frame_index++;
        }
        //ret = av_write_frame(ofmt_ctx, &pkt);
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

        if (ret < 0) {
            LOGE("Error muxing packet\n");
            break;
        }
        av_free_packet(&pkt);

    }

    //Write file trailer
    av_write_trailer(ofmt_ctx);
    end:
    avformat_close_input(&ifmt_ctx);
    (*env)->ReleaseStringUTFChars(env, input_jstr, input_str);
    (*env)->ReleaseStringUTFChars(env, output_jstr, output_str);
    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    if (ret < 0 && ret != AVERROR_EOF) {
        LOGE("Error occurred.\n");
        return -1;
    }
    return 0;
}

/*
* Class:     module_video_jnc_myffmpeg_FFmpegUtils
* Method:    encode
* Signature: (Ljava/lang/String;)I
* 将MP4格式的数据转码成flv（编码格式也改变下h264，改变成随便一个格式。）
 * 转码是先h264 -》 yuv -》 其他格式
*/
JNIEXPORT jint JNICALL Java_module_video_jnc_myffmpeg_FFmpegUtils_encode
        (JNIEnv *env, jclass clazz, jstring jstr_inputPath, jstring jstr_outPath) {

    const char *input_str = (*env)->GetStringUTFChars(env, jstr_inputPath, NULL);
    const char *output_str = (*env)->GetStringUTFChars(env, jstr_outPath, NULL);
    char real_output[100] = {0};
    strcat(real_output, output_str);
    strcat(real_output, "/deocede_yuv.yuv");

    LOGE(" input_str %s ,output str %s ", input_str, real_output);

    AVFormatContext *pFormatCtx;
    int i, videoIndex = -1;
    AVCodecContext *pCodeCtx;
    AVCodec *pCodec;
    AVFrame *pFrameMP4/*, *pFrameYUV*/;
    uint8_t *out_buffer;
    AVPacket *packet;
    int ret, got_pic;
    /**
     * 后面转换可能会用到
     */
    struct SwsContext *img_convert_ctx;

    clock_t time_start, time_finish;
    double time_duration = 0.0;

    av_log_set_callback(custom_log);
    av_register_all();
    avformat_network_init();
    //先分配pFormatCtx
    pFormatCtx = avformat_alloc_context();

    //先为input 初始化pFormatCtx
    if (avformat_open_input(&pFormatCtx, input_str, NULL, NULL) != 0) {
        LOGE(" can't open input stream ");
        return -1;
    }

    //获取流信息，对于一些没有头文件的也有用，文件不会被这个函数改动
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE(" couldn't find stream information . \n");
        return -1;
    }

    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIndex = i;
            break;
        }
    }

    if (videoIndex == -1) {
        LOGE(" find video index faild");
        return -1;
    }

    //这个接口好像已经des。。。后面再来处理warning
    pCodeCtx = pFormatCtx->streams[videoIndex]->codec;

    LOGE(" PCODE CODE ID %d ", pCodeCtx->codec_id);

    pCodec = avcodec_find_decoder(pCodeCtx->codec_id);

    if (pCodec == NULL) {
        LOGE(" couldn't decoder find codec ");
        return -1;
    }

    if (avcodec_open2(pCodeCtx, pCodec, NULL) < 0) {
        LOGE(" open decoder faild ");
        return -1;
    }

    //保存帧分配空间
    pFrameMP4 = av_frame_alloc();
    LOGE(" pCodeCtx->width %d , pCodeCtx->height %d ", pCodeCtx->width, pCodeCtx->height);
    out_buffer = (unsigned char *) av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodeCtx->width, pCodeCtx->height, 1));


    if (out_buffer == NULL) {
        LOGE(" open out_buffer faild ");
        return -1;
    }
    LOGE(" open out_buffer success ");




    packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    /**
     * 分配空间
     */
    img_convert_ctx = sws_getContext(pCodeCtx->width, pCodeCtx->height, pCodeCtx->pix_fmt,
                                     pCodeCtx->width, pCodeCtx->height, AV_PIX_FMT_YUV420P,
                                     SWS_BICUBIC, NULL, NULL, NULL);

    LOGE(" pformatctx %s ", pFormatCtx->iformat->name);
    LOGE(" codec %s ", pCodeCtx->codec->name);
    FILE *out_yuv = fopen(real_output, "wb+");

    if (out_yuv == NULL) {
        LOGE(" OPEN OUTPUT FILE FAILD ");
        return -1;
    }

    char flv_output[100] = {0};
    strcat(flv_output , output_str);
    strcat(flv_output , "/mp4convert.flv");
    AVFormatContext *pOFC ;
    AVOutputFormat *oft ;
    oft = av_guess_format(NULL , flv_output , NULL);
    pOFC = avformat_alloc_context();

    if(pOFC == NULL){
        LOGE(" POFG FAILD ");
        return -1;
    }

    if(oft == NULL){
        LOGE(" guess fmt faild ");
        return -1;
    }
    LOGE(" FORMAT NAME %s ",oft->name);
    pOFC->oformat = oft;

    ret = avio_open(&pOFC->pb , flv_output ,AVIO_FLAG_READ_WRITE );

    if(ret < 0){
        LOGE("AVIO OPEN FAILD ");
        return -1;
    }

    AVStream* video_st;

    video_st = avformat_new_stream(pOFC , 0);

    if(video_st == NULL){
        LOGE(" avformat_new_stream NULL ");
        return -1;
    }
    if(video_st->codec == NULL){
        LOGE(" video_st->codec NULL ");
        return -1;
    }

    video_st->codec->codec_id = pOFC->oformat->video_codec;//oft->video_codec;
    video_st->codec->codec_type=AVMEDIA_TYPE_VIDEO;
    video_st->codec->pix_fmt=AV_PIX_FMT_YUV420P;
    video_st->codec->width = pCodeCtx->width;
    video_st->codec->height = pCodeCtx->height;
    video_st->codec->bit_rate = 400000;
    video_st->codec->gop_size = 250;
    video_st->codec->time_base.num = 1;
    video_st->codec->time_base.den = 25;
    video_st->codec->qmin = 10;
    video_st->codec->qmax = 51;
//    video_st->codec->max_b_frames = 3;
    LOGE(" PCODEC ID %d ", video_st->codec->codec_id);

    if(video_st->codec->codec_id == AV_CODEC_ID_NONE){
        LOGE(" AV_CODEC_ID_NONE ");
        return -1;
    }

    AVCodec *pCEncode = avcodec_find_encoder(video_st->codec->codec_id);

    if(pCEncode == NULL){
        LOGE(" find  Encode faild ");
        return -1 ;
    }
    LOGE(" ENCODE NAME %s ",pCEncode->name);

    ret = avcodec_open2( video_st->codec,  pCEncode , NULL);

    if(ret < 0){
        LOGE(" open encode faild ");
        return -1;
    }

    AVFrame *pFrameFLV ;

    pFrameFLV = av_frame_alloc();

    if(pFrameFLV ==  NULL){
        LOGE(" pFrameFLV ALLOC FAILD ");
        return -1;
    }

    int picture_size = avpicture_get_size(pCodeCtx->pix_fmt , pCodeCtx->width , pCodeCtx->height);

    LOGE(" picture size %d " , picture_size);

    uint8_t *picture_buff = (uint8_t *)av_malloc(picture_size);

    if(picture_buff == NULL){
        LOGE(" picture_buff MALLOC FAILD  ");
        return -1;
    }

    LOGE(" PIX_FMT %d , width %d , height %d" , pCodeCtx->pix_fmt, pCodeCtx->width , pCodeCtx->height);

    /**
     * 对 pFrameFLV 的初始化
     */

//    av_image_fill_arrays(pFrameFLV->data, pFrameFLV->linesize, picture_buff, AV_PIX_FMT_YUV420P,
//                         pCodeCtx->width, pCodeCtx->height, 1);

    avpicture_fill((AVPicture *)pFrameFLV, picture_buff, video_st->codec->pix_fmt, video_st->codec->width, video_st->codec->height);
    avformat_write_header(pOFC , NULL);

    AVPacket *avPacket = (AVPacket *) av_malloc(sizeof(AVPacket) );

    ret = av_new_packet(avPacket , 500);

//    av_init_packet(avPacket);

    if(ret < 0){
        LOGE(" avPacket ALLOC FAILD  ");
        return -1 ;
    }

    int y_size = pCodeCtx->width * pCodeCtx->height;

    int frame_cnt = 0;
    time_start = clock();
    pFrameFLV->format = pCodeCtx->pix_fmt;
    pFrameFLV->width  = pCodeCtx->width;
    pFrameFLV->height = pCodeCtx->height;

    //从MP4文件中读取一帧,保存在packet中
    while (av_read_frame(pFormatCtx, packet) >= 0) {

        LOGE(" read a frame !");
        if (packet->stream_index == videoIndex) {
            //将h264解码成yuv，pFrameMP4已经是yuv数据了。后面再来改变量名。
            ret = avcodec_decode_video2(pCodeCtx, pFrameMP4, &got_pic, packet);
            LOGE(" DEOCDE ret %d , gotpic %d " , ret , got_pic);
            if (ret < 0) {
                LOGE("DECODE ERROR ");
                return -1;
            }
            if(got_pic < 0){
                LOGE(" got pic faild ");
                return -1;
            }
            pFrameFLV->data[0] = pFrameMP4->data[0];
            pFrameFLV->data[1] = pFrameMP4->data[1];
            pFrameFLV->data[2] = pFrameMP4->data[2];
            pFrameFLV->pts = pFrameMP4->pts;

//            fwrite(pFrameFLV->data[0] ,1 ,y_size,  ftest);
//            fwrite(pFrameFLV->data[1], 1, y_size / 4, ftest);  //U
//            fwrite(pFrameFLV->data[2], 1, y_size / 4, ftest);  //V

            LOGE(" CODE W %d , H %d , Frame w %d ,h %d " ,video_st->codec->width,video_st->codec->height ,
                 pFrameFLV->width , pFrameFLV->height);

            //然后将yuv编码成flv,
            ret = avcodec_encode_video2(video_st->codec , avPacket , pFrameFLV , &got_pic);

            LOGE(" encode ret %d , gotpic %d " , ret , got_pic);

            if(ret < 0){
                LOGE(" avcodec_encode_video2 faild ");
                return -1;
            }

            if(got_pic == 1){
                LOGE(" SUCCESS TO ENCODE FRAME ");
                ret = av_write_frame(pOFC , avPacket);
                if(ret < 0){
                    LOGE(" WRITE FRAME FAILD ");
                    return -1;
                }
                av_free_packet(avPacket);
            }
        }

        //记得释放，然后重新装载
        av_free_packet(packet);
}
//    fclose(ftest);
    //Write file trailer
    av_write_trailer(pOFC);

    LOGE(" END .....");
    (*env)->ReleaseStringUTFChars(env, jstr_inputPath, input_str);
    (*env)->ReleaseStringUTFChars(env, jstr_outPath, output_str);
    return 0;
}


JNIEXPORT jint JNICALL Java_module_video_jnc_myffmpeg_FFmpegUtils_encodeYuv
        (JNIEnv * env, jclass clazz , jstring jstr_input, jstring jstr_output){
    av_register_all();
    const char *input_str = (*env)->GetStringUTFChars(env, jstr_input, NULL);
    const char *output_str = (*env)->GetStringUTFChars(env, jstr_output, NULL);
    int width = 480 , height = 272;
    int ret = 0;
    LOGE(" input path %s , output path %s " ,input_str ,  output_str);
    FILE *iFile = fopen(input_str , "r");
    if(iFile == NULL){
        LOGE(" OPEN INPUT FILE FAILD! ");
        return -1;
    }


    AVFormatContext *pOFC ;
    AVOutputFormat *oft ;
    oft = av_guess_format(NULL , output_str , NULL);
    pOFC = avformat_alloc_context();

    if(pOFC == NULL){
        LOGE(" POFG FAILD ");
        return -1;
    }
    if(oft == NULL){
        LOGE(" guess fmt faild ");
        return -1;
    }
    LOGE(" FORMAT NAME %s ",oft->name);
    pOFC->oformat = oft;
    ret = avio_open(&pOFC->pb , output_str ,AVIO_FLAG_READ_WRITE );
    if(ret < 0){
        LOGE(" avio_open faild! ");
        return -1;
    }
    AVStream* video_st;

    video_st = avformat_new_stream(pOFC, 0);

    if (video_st==NULL){
        LOGE(" video_st FAILD !");
        return -1;
    }
    int framecnt = 0;
    video_st->codec->codec_id = pOFC->oformat->video_codec;
    video_st->codec->codec_type=AVMEDIA_TYPE_VIDEO;
    video_st->codec->pix_fmt=AV_PIX_FMT_YUV420P;
    video_st->codec->width = width;
    video_st->codec->height = height;
    video_st->codec->bit_rate = 400000;
    video_st->codec->gop_size = 250;
    video_st->codec->time_base.num = 1;
    video_st->codec->time_base.den = 25;
    video_st->codec->qmin = 10;
    video_st->codec->qmax = 51;

    AVCodec *pCodec = avcodec_find_encoder(video_st->codec->codec_id);
    if(pCodec == NULL){
        LOGE("  pCodec null ");
        return -1;
    }

    if (avcodec_open2(video_st->codec, pCodec,NULL) < 0){
        printf("Failed to open encoder! \n");
        return -1;
    }

    AVFrame *pFrame = av_frame_alloc();
    int pic_size = avpicture_get_size(video_st->codec->pix_fmt, video_st->codec->width, video_st->codec->height);
    LOGE(" pic_size %d " , pic_size);
    uint8_t *picture_buf = (uint8_t *)av_malloc(pic_size);
    avpicture_fill((AVPicture *)pFrame, picture_buf, video_st->codec->pix_fmt, video_st->codec->width, video_st->codec->height);

    //Write File Header
    avformat_write_header(pOFC,NULL);
    AVPacket *pkt = (AVPacket *) av_malloc(sizeof(AVPacket) );

    av_new_packet(pkt,pic_size);

    int y_size = video_st->codec->width * video_st->codec->height;
    int i = 0 ;
    while(fread(picture_buf , 1 , y_size * 3 / 2 ,iFile)){
        pFrame->data[0] = picture_buf;              // Y
        pFrame->data[1] = picture_buf+ y_size;      // U
        pFrame->data[2] = picture_buf+ y_size*5/4;  // V
        //PTS
        //pFrame->pts=i;
        pFrame->pts=i*(video_st->time_base.den)/((video_st->time_base.num)*25);

        int got_picture=0;
        //Encode
        int ret = avcodec_encode_video2(video_st->codec, pkt,pFrame, &got_picture);
        if(ret < 0){
            LOGE(" FAILD ENCODE ");
            return -1;
        }

        if(got_picture == 1){
            LOGE(" ENCODE success %d" , framecnt );
            framecnt ++;
            pkt->stream_index = video_st->index;
            ret = av_write_frame(pOFC, pkt);
            av_free_packet(pkt);

        }
        ++i;
    }
    av_write_trailer(pOFC);
    LOGE(" end... ");

    //Clean
    if (video_st){
        avcodec_close(video_st->codec);
        av_free(pFrame);
        av_free(picture_buf);
    }
    avio_close(pOFC->pb);
    avformat_free_context(pOFC);

    fclose(iFile);
    (*env)->ReleaseStringUTFChars(env, jstr_input, input_str);
    (*env)->ReleaseStringUTFChars(env, jstr_output, output_str);

}


/*
* Class:     module_video_jnc_myffmpeg_FFmpegUtils
* Method:    addfilter
* Signature: (Ljava/lang/String;Ljava/lang/String;)I
 * 打水印并输出yuv文件
 *
 * http://ffmpeg.org/doxygen/3.2/filtering_video_8c-example.html#a48
*/
JNIEXPORT jint JNICALL Java_module_video_jnc_myffmpeg_FFmpegUtils_addfilter
        (JNIEnv *env, jclass clazz , jstring inputStr, jstring outputStr){
//    const char *filter_descr = "movie=/storage/emulated/0/FFmpeg/filter.PNG[wm];[in][wm]overlay=5:5[out]";
    const char *filter_descr = "scale=78:24,transpose=cclock";
    const char *input_str = (*env)->GetStringUTFChars(env, inputStr, NULL);
    const char *output_str = (*env)->GetStringUTFChars(env, outputStr, NULL);
    LOGE(" input str %s , outputstr %s" ,input_str ,  output_str);
//
//    AVFormatContext *pFormatCtx;
//    int i, videoindex;
//
//    /**
//     * 每个avStream存储一个视频/音频流的相关数据；每个avstream对应一个AVCodecContext，
//     * 存储该视频/音频流使用解码方式相关数据；
//     * 每个AVCodecContext中对应一个avcodec，包含该视频/音频对应的解码器。每种解码器都对应一个avcodec结构。
//     */
//    AVCodecContext *pCodecCtx;
//    //存储编解码器的结构体
//    AVCodec *pCodec;
//    //解码后的数据AVFrame
//    AVFrame *pFrame ;
//    uint8_t *out_buffer;
//    //解码前的数据
//    AVPacket *packet;
//    int y_size;
//    int ret, got_picture;
//    FILE *fp_yuv;
//    int frame_cnt;
//    clock_t time_start, time_finish;
//    double time_duration = 0.0;
//    char info[1000] = {0};
//
//    /**
//     * 初始化，avformat，然后还初始化混合器，分流器。
//     */
//    av_register_all();
//
//    //FFmpeg av_log() callback
//    av_log_set_callback(custom_log);
//    /**
//     * 初始化全局的网络模块，可选的，但是推荐初始化，如果使用了网络协议，必须强制初始化
//     */
//    avformat_network_init();
//    /**
//     * AVFormatContext,分配
//     */
//    pFormatCtx = avformat_alloc_context();
//
//
//
//
//    /**
//     * 打开输入流，注意使用avformat_close_input关闭，
//     * 注意传入的是pFormatCtx的地址(pFormatCtx本身也是一个指针变量)（需要用二级指针来保存），
//     */
//    if (avformat_open_input(&pFormatCtx, input_str, NULL, NULL) != 0) {
//        LOGE("Couldn't open input stream.\n");
//        return -1;
//    }
//    /**
//     * 读取文件的信息，第二个参数是AVDictionary（dict.h）,不过推荐使用tree.h中的。因为这个太大效率会低下
//     */
//    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
//        LOGE("Couldn't find stream information.\n");
//        return -1;
//    }
//
//    videoindex = -1;
//
//    for (i = 0; i < pFormatCtx->nb_streams; i++) {
//        //获取video轨道
//        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
//            videoindex = i;
//            break;
//        }
//    }
//
//    if (videoindex == -1) {
//        LOGE("Couldn't find a video stream.\n");
//        return -1;
//    }
//
//    pCodecCtx = pFormatCtx->streams[videoindex]->codec;
//
//    /**
//     * Find a registered decoder with a matching codec ID.
//     * 找到一个解码器，如果没有就返回NULL
//     */
//    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
//
//    if (pCodec == NULL) {
//        LOGE("Couldn't find Codec.\n");
//        return -1;
//    }
//
//    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
//        LOGE("Couldn't open codec.\n");
//        return -1;
//    }
//
//    /**
//     * 为解码后存储数据的pframe分配空间
//     */
//    pFrame = av_frame_alloc();
//
//    /**
//     * 为解码前申请空间
//     */
//    packet = (AVPacket *) av_malloc(sizeof(AVPacket));
//    LOGE("[Input     ]%s\n", input_str);
//    LOGE("%s[Output    ]%s\n", info, output_str);
//    LOGE("%s[Format    ]%s\n", info, pFormatCtx->iformat->name);
//    LOGE("%s[Codec     ]%s\n", info, pCodecCtx->codec->name);
//    LOGE("%s[Resolution]%dx%d\n", info, pCodecCtx->width, pCodecCtx->height);
//
//    /**
//     * 读写方式打开文件，不是追加
//     */
//    fp_yuv = fopen(output_str, "wb+");
//
//    if (fp_yuv == NULL) {
//        printf("Cannot open output file.\n");
//        return -1;
//    }
//
//    frame_cnt = 0;
//    time_start = clock();
//
///********************************水印初始化*****************************/
//
//    avfilter_register_all();
//    char args[512] ;
//    AVFilterContext *buffersink_ctx;
//    AVFilterContext *buffersrc_ctx;
//    AVFilterGraph *filter_graph = avfilter_graph_alloc();
//    if(filter_graph == NULL){
//        LOGE(" filter_graph  FAILD ");
//    }
//
//    AVFilter *buffersrc = avfilter_get_by_name("buffer");
//    if(buffersrc == NULL){
//        LOGE(" BUFFER SRC FAILD ");
//    }
//    AVFilter *buffersink = avfilter_get_by_name("buffersink");
//    if(buffersink == NULL){
//        LOGE(" buffersink  FAILD ");
//    }
//    AVFilterInOut *outputs = avfilter_inout_alloc();
//    if(outputs == NULL){
//        LOGE(" outputs  FAILD ");
//    }
//
//    AVFilterInOut *inputs = avfilter_inout_alloc();
//    if(inputs == NULL){
//        LOGE(" inputs  FAILD ");
//    }
//    enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420P , AV_PIX_FMT_NONE};
//
//    AVBufferSinkParams *buffersink_params;
//
//
//    ret = avfilter_graph_create_filter(&buffersrc_ctx , buffersrc , "in" , args , NULL , filter_graph);
//    LOGE(" CREATE FILTER RET %d" , ret);
//    if(ret < 0){
//        LOGE("create in filter faild");
//        return -1 ;
//    }
//    buffersink_params = av_buffersink_params_alloc();
//    buffersink_params->pixel_fmts = pix_fmts;
//    ret = avfilter_graph_create_filter(&buffersink_ctx , buffersink , "out",  NULL ,buffersink_params , filter_graph);
//
//    if(ret < 0){
//        LOGE("create out filter faild");
//        return -1 ;
//    }
//
//    outputs->name = av_strdup("in");
//    outputs->filter_ctx = buffersink_ctx;
//    outputs->pad_idx = 0;
//    outputs->next = NULL;
//
//
//    inputs->name =av_strdup("out");
//    inputs->filter_ctx = buffersink_ctx;
//    inputs->pad_idx = 0;
//    inputs->next = NULL;
//
//    ret = avfilter_graph_parse_ptr(filter_graph , filter_descr , &inputs , &outputs , NULL);
//    if(ret < 0){
//        LOGE("avfilter_graph_parse_ptr faild");
//        return -1;
//    }
//
//    AVFrame *pFrame_out = av_frame_alloc();
//
//    /***************************************水印初始化结束*************************/
//
//
//    while (av_read_frame(pFormatCtx, packet) >= 0) {
//        if (packet->stream_index == videoindex) {
//            /**
//             * 解码
//             */
//            ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
//
//            if (ret < 0) {
//                LOGE("Decode Error.\n");
//                return -1;
//            }
//            /**
//             * 如果拿到图像帧
//             */
//            if (got_picture) {
//                pFrame->pts = av_frame_get_best_effort_timestamp(pFrame);
//                LOGE(" PTS  %d " , pFrame->pts );
//
//                if(av_buffersrc_add_frame(buffersrc_ctx , pFrame) < 0 ){
//                    LOGE("error  while feeding the filtergraph ");
//                    break;
//                }
//                while(1){
//                    ret = av_buffersink_get_frame(buffersink_ctx ,pFrame_out );
//                    if(ret < 0){
//                        LOGE("av_buffersink_get_frame");
//                        break;
//                    }
//                    LOGE(" PROCESS 1 FRAME !");
//                    if(pFrame_out ->format == AV_PIX_FMT_YUV420P){
//                        //Y, U, V
//                        for(int i=0;i<pFrame_out->height;i++){
//                            fwrite(pFrame_out->data[0]+pFrame_out->linesize[0]*i,1,pFrame_out->width,fp_yuv);
//                        }
//                        for(int i=0;i<pFrame_out->height/2;i++){
//                            fwrite(pFrame_out->data[1]+pFrame_out->linesize[1]*i,1,pFrame_out->width/2,fp_yuv);
//                        }
//                        for(int i=0;i<pFrame_out->height/2;i++){
//                            fwrite(pFrame_out->data[2]+pFrame_out->linesize[2]*i,1,pFrame_out->width/2,fp_yuv);
//                        }
//                    }
//                    av_frame_unref(pFrame_out);
//                }
////                /**
////                 * 按yuv420方式写入文件中。
////                 */
////                y_size = pCodecCtx->width * pCodecCtx->height;
////                fwrite(pFrame->data[0], 1, y_size, fp_yuv);    //Y
////                fwrite(pFrame->data[1], 1, y_size / 4, fp_yuv);  //U
////                fwrite(pFrame->data[2], 1, y_size / 4, fp_yuv);  //V
////                //Output info
////                char pictype_str[10] = {0};
////                switch (pFrame->pict_type) {
////                    case AV_PICTURE_TYPE_I:
////                        LOGE("decode  %s I", pictype_str);
////                        break;
////                    case AV_PICTURE_TYPE_P:
////                        LOGE("decode  %s P", pictype_str);
////                        break;
////                    case AV_PICTURE_TYPE_B:
////                        LOGE("decode  %s B", pictype_str);
////                        break;
////                    default:
////                        LOGE("decode  %s Other", pictype_str);
////                        break;
////                }
////                LOGI("Frame Index: %5d. Type:%s", frame_cnt, pictype_str);
//                frame_cnt++;
//            }
//            av_frame_unref(pFrame);
//        }
//        av_free_packet(packet);
//    }
//    /**
//     * 清空decoder，如果有数据就写入，没有数据就退出。
//     */
////    while (1) {
////        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
////        if (ret < 0) {
////            LOGE("ret < 0 break ");
////            break;
////        }
////
////        if (!got_picture) {
////            LOGE(" got_picture %d break ", got_picture);
////            break;
////        }
////
////        int y_size = pCodecCtx->width * pCodecCtx->height;
////        fwrite(pFrame->data[0], 1, y_size, fp_yuv);    //Y
////        fwrite(pFrame->data[1], 1, y_size / 4, fp_yuv);  //U
////        fwrite(pFrame->data[2], 1, y_size / 4, fp_yuv);  //V
////        //Output info
////        char pictype_str[10] = {0};
////        switch (pFrame->pict_type) {
////            case AV_PICTURE_TYPE_I:
////                sprintf(pictype_str, "I");
////                LOGE("I %s", pictype_str);
////                break;
////            case AV_PICTURE_TYPE_P:
////                LOGE("P %s", pictype_str);
////                break;
////            case AV_PICTURE_TYPE_B:
////                LOGE("B %s", pictype_str);
////                break;
////            default:
////                LOGE("Other ");
////                break;
////        }
////        LOGE("Frame Index: %5d. Type :%s ", frame_cnt, pictype_str);
////        frame_cnt++;
////    }
//
//    time_finish = clock();
//    time_duration = (double) (time_finish - time_start);
//
//    LOGE("%s[Time      ]%fms\n", info, time_duration);
//    LOGE("%s[Count     ]%d\n", info, frame_cnt);
//    fclose(fp_yuv);
//    avfilter_graph_free(&filter_graph);
//
//
//
//    av_frame_free(&pFrame);
//    avcodec_close(pCodecCtx);
//    avformat_close_input(&pFormatCtx);
    main2(input_str , output_str);
    (*env)->ReleaseStringUTFChars(env, inputStr, input_str);
    (*env)->ReleaseStringUTFChars(env, outputStr, output_str);
    return 0;
}

/*******************官网测试代码************/
const char *filter_descr = "scale=78:24,transpose=cclock";
/* other way:
   scale=78:24 [scl]; [scl] transpose=cclock // assumes "[in]" and "[out]" to be input output pads respectively
 */
static AVFormatContext *fmt_ctx;
static AVCodecContext *dec_ctx;
AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
static int video_stream_index = -1;
static int64_t last_pts = AV_NOPTS_VALUE;
static int open_input_file(const char *filename)
{
    int ret;
    AVCodec *dec;
    if ((ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
        LOGE( "Cannot open input file\n");
        return ret;
    }
    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        LOGE(  "Cannot find stream information\n");
        return ret;
    }
    /* select the video stream */
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (ret < 0) {
        LOGE(  "Cannot find a video stream in the input file\n");
        return ret;
    }
    video_stream_index = ret;
    dec_ctx = fmt_ctx->streams[video_stream_index]->codec;
    av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);
    /* init the video decoder */
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        LOGE(  "Cannot open video decoder\n");
        return ret;
    }
    return 0;
}
static int init_filters(const char *filters_descr)
{
    char args[512];
    int ret = 0;
    AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVRational time_base = fmt_ctx->streams[video_stream_index]->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    /* buffer video source: the decoded frames from the decoder will be inserted here. */
//    snprintf(args, sizeof(args),
//             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
//             dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
//             time_base.num, time_base.den,
//             dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);
    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        LOGE(  "Cannot create buffer source\n");
        goto end;
    }
    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        LOGE( "Cannot create buffer sink\n");
        goto end;
    }
    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        LOGE( "Cannot set output pixel format\n");
        goto end;
    }
    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */
    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;
    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;
    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;
    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;
    end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return ret;
}
//static void display_frame(const AVFrame *frame, AVRational time_base)
//{
//    int x, y;
//    uint8_t *p0, *p;
//    int64_t delay;
//    if (frame->pts != AV_NOPTS_VALUE) {
//        if (last_pts != AV_NOPTS_VALUE) {
//            /* sleep roughly the right amount of time;
//             * usleep is in microseconds, just like AV_TIME_BASE. */
//            delay = av_rescale_q(frame->pts - last_pts,
//                                 time_base, AV_TIME_BASE_Q);
//            if (delay > 0 && delay < 1000000)
//                usleep(delay);
//        }
//        last_pts = frame->pts;
//    }
//    /* Trivial ASCII grayscale display. */
//    p0 = frame->data[0];
//    puts("\033c");
//    for (y = 0; y < frame->height; y++) {
//        p = p0;
//        for (x = 0; x < frame->width; x++)
//            putchar(" .-+#"[*(p++) / 52]);
//        putchar('\n');
//        p0 += frame->linesize[0];
//    }
//    fflush(stdout);
//}
int main2(char *inputfile , char *outputFile)
{
    int ret;
    AVPacket packet;
    AVFrame *frame = av_frame_alloc();
    AVFrame *filt_frame = av_frame_alloc();
    int got_frame;
    if (!frame || !filt_frame) {
        LOGE("Could not allocate frame");
    }
    av_register_all();
    avfilter_register_all();
    if ((ret = open_input_file(inputfile) < 0))
        goto end;
    if ((ret = init_filters(filter_descr)) < 0)
        goto end;
    /* read all packets */
    while (1) {
        if ((ret = av_read_frame(fmt_ctx, &packet)) < 0)
            break;
        if (packet.stream_index == video_stream_index) {
            got_frame = 0;
            ret = avcodec_decode_video2(dec_ctx, frame, &got_frame, &packet);
            if (ret < 0) {
                LOGE(NULL, AV_LOG_ERROR, "Error decoding video\n");
                break;
            }
            if (got_frame) {
                frame->pts = av_frame_get_best_effort_timestamp(frame);
                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    LOGE(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                    break;
                }
                /* pull filtered frames from the filtergraph */
                while (1) {
                    ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if (ret < 0)
                        goto end;
//                    display_frame(filt_frame, buffersink_ctx->inputs[0]->time_base);
                    av_frame_unref(filt_frame);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(&packet);
    }
    end:
    avfilter_graph_free(&filter_graph);
    avcodec_close(dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);
    if (ret < 0 && ret != AVERROR_EOF) {
        LOGE(stderr, "Error occurred: %s\n", av_err2str(ret));
    }
}
  
  
  
  
  
  
  
  
  