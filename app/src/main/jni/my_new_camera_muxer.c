#include "my_camera_muxer.h"
#include "My_LOG.h"
#include <string.h>
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
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
//http://blog.csdn.net/bixinwei22/article/details/78779259

AVOutputFormat *ofmt = NULL;
const char *mp4_output_path;
int width , height ,audio_size;
AVFormatContext *ofmt_ctx ;
AVCodec *avVideoCode;
AVStream *video_stream;
AVCodec *avAudioCode;
AVStream *audio_stream;
int video_gop_size = 20;
SwrContext *swr;
uint8_t *outs[2];
int y_size;
AVFrame *video_frame;
int videoFrameCount = 0;
int audioFrameCount = 0;
AVPacket videoPacket;
AVPacket audioPacket;
int64_t video_last_pts ,audio_last_pts;

int init_camera_muxer(const char *outputPath , int w , int h , int aSize){
    int ret = 0;
    mp4_output_path = outputPath;
    width = w ;
    height = h;
    y_size = w * h ;
    audio_size = aSize;
    LOGE("W %d , h %d ,y_size %d" , w , h , y_size);
    av_register_all();
    av_log_set_callback(custom_log);
//    ofmt = av_guess_format(NULL , outputPath , NULL);
    ret = avformat_alloc_output_context2(&ofmt_ctx , NULL , NULL , outputPath);
    if(ret < 0){
        LOGE("AVFormatContext ALLOC CONTEXT FAILD !");
        return -1;
    }
    ofmt = ofmt_ctx->oformat;
    if(ofmt == NULL){
        LOGE("AVOutputFormat FAILD!");
        return -1;
    }
    if(ofmt->audio_codec != AV_CODEC_ID_NONE){
        ret = initMuxerAudio();
        if(ret < 0){
            return -1;
        }
    }

    if(ofmt->video_codec != AV_CODEC_ID_NONE){
        ret = initMuxerVideo();
        if(ret < 0){
            return -1;
        }
    }

    if(ofmt->subtitle_codec != AV_CODEC_ID_NONE){
    }

    if(!(ofmt->flags & AVFMT_NOFILE)){
        if(avio_open(&ofmt_ctx->pb , outputPath , AVIO_FLAG_WRITE) < 0){
            LOGE("OPEN OUTPUT FILE FAILD !");
            return -1;
        }
    }

    ret = avformat_write_header(ofmt_ctx , NULL);
    av_init_packet(&audioPacket);
    av_init_packet(&videoPacket);


    if(ret < 0){
        LOGE(" WRITE HEADER FAILD !");
        return -1;
    }
    LOGE(" INIT SUCCESS ！");

    return ret;
}


int initMuxerVideo(){
    int ret = 0 ;
    avVideoCode = avcodec_find_encoder(ofmt->video_codec);
    if(avVideoCode == NULL){
        LOGE("FIND VIDEO CODE FAILD!");
        return -1;
    }
    video_stream = avformat_new_stream(ofmt_ctx , avVideoCode);
    if(video_stream == NULL){
        LOGE(" VIDEO STREAM ALLOC FAILD !");
        return -1;
    }
    AVCodecContext *vCodeContext = video_stream->codec;
    vCodeContext->codec_type = AVMEDIA_TYPE_VIDEO;
    vCodeContext->bit_rate = 400000;
    vCodeContext->width = width;
    vCodeContext->height = height;
    vCodeContext->time_base.den = 25;
    vCodeContext->time_base.num = 1;
    vCodeContext->gop_size = video_gop_size;
    vCodeContext->pix_fmt = AV_PIX_FMT_YUV420P;
    vCodeContext->qmin = 10;
    vCodeContext->qmax = 51;
//    vCodeContext->qcompress = 0.6f;
//    vCodeContext->max_b_frames = 0;
    video_stream->time_base.den = 90000;
    video_stream->time_base.num = 1;
    ret = avcodec_open2(video_stream->codec , avVideoCode , NULL);
    if(ret < 0){
        LOGE(" VIDEO AVCODE OPEN FAILD !");
        return -1;
    }
    video_frame = av_frame_alloc();
    video_frame->format = video_stream->codec->pix_fmt;
    video_frame->width  = video_stream->codec->width;
    video_frame->height = video_stream->codec->height;
//    uint8_t *data[4];
//    int linesize[4]; //这个不知道怎么使用。有问题
//    ret = av_image_fill_arrays(data , linesize , NULL , video_stream->codec->pix_fmt , width , height ,0);


    int pic_size = avpicture_get_size(video_stream->codec->pix_fmt, video_stream->codec->width,
                                      video_stream->codec->height);

    LOGE(" pic_size %d ", pic_size);

    uint8_t *picture_buf = (uint8_t *) av_malloc(pic_size);
    /**
     * Setup the data pointers and linesizes based on the specified image parameters and the provided array.
     */
    avpicture_fill((AVPicture *) video_frame, picture_buf, video_stream->codec->pix_fmt,
                   video_stream->codec->width, video_stream->codec->height);

    if(ret < 0){
        LOGE(" AVIMAGE FILL ARRAY FAILD !");
        return -1;
    }

    // some formats want stream headers to be separate
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        ofmt_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
    LOGE(" INIT VIDEO SUCCESS !");
    return 1;
}


int initMuxerAudio(){
    int ret = 0;
    avAudioCode = avcodec_find_encoder(ofmt->audio_codec);
    if(avAudioCode == NULL){
        LOGE("FIND AUDIO CODE FAILD!");
        return -1;
    }
    audio_stream = avformat_new_stream(ofmt_ctx , avAudioCode);
    if(audio_stream == NULL){
        LOGE(" AUDIO STREAM ALLOC FAILD !");
        return -1;
    }
    AVCodecContext *audio_context = audio_stream->codec;
    audio_context->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_context->channel_layout = AV_CH_LAYOUT_MONO;
    audio_context->channels = av_get_channel_layout_nb_channels(audio_context->channel_layout);
    audio_context->bit_rate = 64000;
    audio_context->sample_rate = 44100;
    audio_context->sample_fmt = AV_SAMPLE_FMT_FLTP;//*avAudioCode->sample_fmts;
    ret = avcodec_open2(audio_context , avAudioCode , NULL);
    if(ret < 0){
        LOGE(" avcodec_open2 AUDIO FAILD ! ");
        return -1;
    }
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        ofmt_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
    LOGE(" INIT AUDIO MUXER SUCCESS !");
    return 1;
}

int init_muxer_Sws(){
    swr = swr_alloc();
    av_opt_set_int(swr, "in_channel_layout",  AV_CH_LAYOUT_MONO, 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_MONO,  0);
    av_opt_set_int(swr, "in_sample_rate",     16000, 0);
    av_opt_set_int(swr, "out_sample_rate",    16000, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt",  AV_SAMPLE_FMT_S16, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP,  0);
    swr_init(swr);
    outs[0]=(uint8_t *)malloc(audio_size);
    outs[1]=(uint8_t *)malloc(audio_size);
}

int encodeCamera_muxer(jbyte *nativeYuv){
    video_frame->data[0] = (uint8_t *)nativeYuv;
    video_frame->data[1] = (uint8_t *) nativeYuv + y_size;
    video_frame->data[2] =(uint8_t *) nativeYuv + y_size * 5 / 4;
    int got_picture;
    if(avcodec_encode_video2(video_stream->codec , &videoPacket ,video_frame , &got_picture ) < 0){
        LOGE("ENCODE VIDEO FAILD ！");
        return -1;
    }
    if(got_picture == 1){
            videoPacket.duration = av_rescale_q(1, video_stream->codec->time_base, video_stream->time_base);
            videoPacket.pts = videoFrameCount * videoPacket.duration;
            videoPacket.dts = videoPacket.pts;
            videoPacket.stream_index = video_stream->index;
            LOGE("duration %lld , video pts %lld " ,  videoPacket.duration  ,videoPacket.pts );
            video_last_pts = videoPacket.pts;
//        interleaved_write(&videoPacket , NULL);
        av_write_frame(ofmt_ctx , &videoPacket);
        av_free_packet(&videoPacket);
        videoFrameCount ++;
    }

    return -1;
}


int encodeAudio_muxer(jbyte *nativePcm){

    return -1;
}

int close_muxer(){
    av_write_trailer(ofmt_ctx);
    LOGE("CLOSE SUCCESS ");
    return -1;
}
int interleaved_write(AVPacket *yuvPkt , AVPacket *pcmPkt){
    if(av_interleaved_write_frame(ofmt_ctx, yuvPkt) < 0){
        LOGE(" WRITE VIDEO_FRAME FAILD ! ");
        return -1;
    }

    if(yuvPkt != NULL){
        av_free_packet(&videoPacket);
    }

    if(pcmPkt != NULL){
        av_free_packet(pcmPkt);
    }

    return -1;
}










