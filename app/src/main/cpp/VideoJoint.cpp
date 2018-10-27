//
// Created by Administrator on 2018/10/25/025.
//
#include "VideoJoint.h"

/**
 * 拼接所有不一定的视频都是相同的分辨率
 * 所以所有输入的视频都需要把分辨率转成相同的。
 * 流程
 * 1.输出先确定（视频流，输出流）
 * 2.再确定输入（视频流，输出流）
 * 3.初始化音频重采样
 * 4.初始化视频裁剪器
 * 5.输入的音频需要重采样
 * 6.输入的视频需要重改分辨率
 * 7.编码
 * 8.混合
 */

VideoJoint::VideoJoint(vector<char *> inputPath, const char *output, int outWidth, int outHeight) {
    this->outWidth = outWidth;
    this->outHeight = outHeight;
    this->inputPaths = inputPath;

    int pathLen = strlen(output);
    pathLen++;
    this->outPath = (char *) malloc(pathLen);
    strcpy(this->outPath, output);
    LOGE(" OUTPUT path %s ", this->outPath);
    initValue();
}

void VideoJoint::initValue() {
    videoIndexInput = -1;
    audioIndexInput = -1;
    videoCodecD = NULL;
    videoCodecE = NULL;
    audioCodecE = NULL;
    audioCodecD = NULL;
    aCtxD = NULL;
    aCtxE = NULL;
    vCtxD = NULL;
    vCtxE = NULL;
    afc_input = NULL;
    afc_output = NULL;
    afot = NULL;
    audioOutStream = NULL;
    videoOutStream = NULL;
    sws = NULL;
    swc = NULL;

    sampleRate = 44100;
    sampleFormat = AV_SAMPLE_FMT_FLTP;
    outChannelLayout = AV_CH_LAYOUT_MONO;
    channel = av_get_channel_layout_nb_channels(outChannelLayout);
    nbSample = 1024;
    audioOutBuffer = (uint8_t *) malloc(av_get_bytes_per_sample(sampleFormat) * nbSample);

    audioQueMax = 120;
    videoQueMax = 100;

}

int VideoJoint::initSwrContext(int channelCount, AVSampleFormat in_sample_fmt, int in_sample_rate) {

    //这个地方很奇怪，这里就有用，不然会崩溃。
    swc = swr_alloc();
    if (swc == NULL) {
        LOGE(" SWC ALLOC FAILD !");
        return -1;
    }
    swc = swr_alloc_set_opts(swc,
                             av_get_default_channel_layout(channel),
                             sampleFormat, sampleRate,
                             av_get_default_channel_layout(channelCount),
                             in_sample_fmt, in_sample_rate,
                             0, 0);

    if (swc == NULL) {
        LOGE(" swc NULL !");
        return -1;
    }
    if (swr_init(swc) < 0) {
        LOGE(" swr_init FAILD !");
        return -1;
    }
    return 1;
}


void VideoJoint::destroySwrContext() {
    if (swc != NULL) {
        swr_free(&swc);
        swc = NULL;
    }
}

int VideoJoint::initSwsContext(int inWidth, int inHeight, int inpixFmt) {
    sws = sws_getContext(inWidth, inHeight, (AVPixelFormat) inpixFmt, outWidth, outHeight,
                         AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
    if (sws == NULL) {
        return -1;
    }
    return 1;
}

void VideoJoint::destroySwsContext() {
    if (sws != NULL) {
        sws_freeContext(sws);
        sws = NULL;
    }
}

void VideoJoint::startJoint() {
    av_register_all();
    avcodec_register_all();
#ifdef DEBUG
    av_log_set_callback(custom_log);
#endif
    this->start();
    int result = initOutput(this->outPath);
    if (result < 0) {
        LOGE(" init output faild !");
        return;
    }
    for (int i = 0; i < inputPaths.size(); ++i) {
        destroyInput();
        LOGE(" INPUT path %s ", inputPaths.at(i));
        result = initInput(inputPaths.at(i));
        if (result < 0) {
            LOGE(" initinput faild !");
            return;
        }
        startDecode();
        break;
    }
    av_write_trailer(afc_output);
}

void VideoJoint::run() {

    while(!isExit){
        if(this->pause || audioOutStream == NULL || videoOutStream == NULL){
            threadSleep(2);
            continue;
        }
        if(audioQue.size() <= 0 || videoQue.size() <= 0){
            continue;
        }

        AVPacket *aPkt = audioQue.front();
        AVPacket *vPkt = videoQue.front();
        if(av_compare_ts(aPkt->pts , audioOutStream->time_base , vPkt->pts , videoOutStream->time_base) < 0){
            av_interleaved_write_frame(afc_output ,aPkt );
            audioQue.pop();
            av_packet_free(&aPkt);
        }
        else{
            av_interleaved_write_frame(afc_output ,vPkt );
            videoQue.pop();
            av_packet_free(&vPkt);
        }
    }
}

AVFrame *VideoJoint::deocdePacket(AVPacket *packet, AVCodecContext *decode) {

    int result = avcodec_send_packet(decode, packet);
    if (result < 0) {
        LOGE("  avcodec_send_packet %s ", av_err2str(result));
        return NULL;
    }
    AVFrame *frame = av_frame_alloc();
    while (result >= 0) {
        result = avcodec_receive_frame(decode, frame);
        if (result < 0) {
            LOGE(" avcodec_receive_frame  faild %s ", av_err2str(result));
            av_frame_free(&frame);
            return NULL;
        }
        return frame;
    }
    av_frame_free(&frame);
    return NULL;
}


AVPacket *VideoJoint::encodeFrame(AVFrame *frame, AVCodecContext *encode) {
    int result = 0;
    result = avcodec_send_frame(encode, frame);
    if (result < 0) {
        LOGE(" avcodec_send_frame faild ! %s ", av_err2str(result));
        return NULL;
    }
    AVPacket *packet = av_packet_alloc();
    while (result >= 0) {
        result = avcodec_receive_packet(encode, packet);
        if (result < 0) {
            LOGE(" avcodec_receive_packet faild ! %s ", av_err2str(result));
            av_packet_free(&packet);
            return NULL;
        }
        return packet;
    }
    return NULL;
}

void VideoJoint::startDecode() {
    //开始解码
    int result = 0;
    AVPacket *pkt = av_packet_alloc();
    while (!isExit) {
        LOGE(" AUDIO QUE size %d , video que size %d" ,audioQue.size() , videoQue.size()  );
//        if(audioQue.size() > audioQueMax || videoQue.size() > videoQueMax){
//            threadSleep(2);
//            continue;
//        }
        result = av_read_frame(afc_input, pkt);
        if (result < 0) {
            LOGE(" startDecode av_read_frame FAILD ! %s ", av_err2str(result));
            break;
        }
        AVFrame *frame = NULL;
        if (pkt->stream_index == videoIndexInput) {
            frame = deocdePacket(pkt, vCtxD);
            if (frame != NULL) {
                LOGE(" WRITE VIDEO ");
                //修改分辨率
                sws_scale(sws, (const uint8_t *const *) frame->data, frame->linesize,
                          0, frame->height, outVFrame->data, outVFrame->linesize);
                av_frame_free(&frame);
                AVPacket *vPkt = encodeFrame(outVFrame, vCtxE);
                if (vPkt != NULL) {
                    //放入队列
                    vPkt->stream_index = videoIndexOutput;
                    addQueue(vPkt);
                    LOGE(" VIDEO PKT PTS %lld " , vPkt->pts);
                }
            }
        } else if (pkt->stream_index == audioIndexInput) {
            frame = deocdePacket(pkt, aCtxD);
            if (frame != NULL) {
                result = swr_convert(swc, &audioOutBuffer, frame->nb_samples,
                                     (const uint8_t **) frame->data, frame->nb_samples);
                if (result < 0) {
                    LOGE(" swr_convert faild ! %s  ", av_err2str(result));
                    continue;
                }
                LOGE(" AUDIO RETURN NB_SAMPLE %d ", result);
                AVPacket *aPkt = encodeFrame(frame, aCtxE);
                if (aPkt != NULL) {
                    //放入队列
                    aPkt->stream_index = audioIndexOutput;
                    addQueue(aPkt);
                    LOGE(" audio PKT PTS %lld " , aPkt->pts);
                }
                av_frame_free(&frame);
            }
        }
    }
}


void VideoJoint::addQueue(AVPacket *pkt) {
    if (pkt->stream_index == videoIndexInput) {
        pkt->pts = av_rescale_q_rnd(pkt->pts, afc_input->streams[videoIndexInput]->time_base,
                                    videoOutStream->time_base,
                                    AV_ROUND_NEAR_INF);
        pkt->dts = av_rescale_q_rnd(pkt->dts, afc_input->streams[videoIndexInput]->time_base,
                                    videoOutStream->time_base,
                                    AV_ROUND_NEAR_INF);
        pkt->duration = av_rescale_q(pkt->duration, afc_input->streams[videoIndexInput]->time_base,
                                     videoOutStream->time_base);
        videoQue.push(pkt);
    } else if (pkt->stream_index == audioIndexOutput) {
        pkt->pts = av_rescale_q_rnd(pkt->pts, afc_input->streams[audioIndexInput]->time_base,
                                     audioOutStream->time_base,
                                     AV_ROUND_NEAR_INF);
        pkt->dts = av_rescale_q_rnd(pkt->dts, afc_input->streams[audioIndexInput]->time_base,
                                     audioOutStream->time_base,
                                     AV_ROUND_NEAR_INF);
        pkt->duration = av_rescale_q(pkt->duration,
                                      afc_input->streams[audioIndexInput]->time_base,
                                      audioOutStream->time_base);
        audioQue.push(pkt);
    }
}


int VideoJoint::initInput(char *path) {
    int result = 0;
    afc_input = NULL;
    result = avformat_open_input(&afc_input, path, 0, 0);
    if (result != 0) {
        LOGE("avformat_open_input FAILD !");
        return -1;
    }
    result = avformat_find_stream_info(afc_input, 0);
    if (result != 0) {
        LOGE("avformat_find_stream_info failed!:%s", av_err2str(result));
        return -1;
    }
    for (int i = 0; i < afc_input->nb_streams; ++i) {
        AVStream *stream = afc_input->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIndexInput = i;
            videoCodecD = avcodec_find_decoder(stream->codecpar->codec_id);
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioIndexInput = i;
            audioCodecD = avcodec_find_decoder(stream->codecpar->codec_id);
        }
    }

//  swsConext
    destroySwsContext();
    int inWidth = afc_input->streams[videoIndexInput]->codecpar->width;
    int inHeight = afc_input->streams[videoIndexInput]->codecpar->height;
    int pixFmt = afc_input->streams[videoIndexInput]->codecpar->format;
    result = initSwsContext(inWidth, inHeight, (AVPixelFormat) pixFmt);

    if (result < 0) {
        LOGE("initSwsContext FAILD !");
        return -1;
    }

    destroySwrContext();
    int sampleRate = afc_input->streams[audioIndexInput]->codecpar->sample_rate;
    AVSampleFormat sampleFmt = (AVSampleFormat) afc_input->streams[audioIndexInput]->codecpar->format;
    int channelCount = (AVSampleFormat) afc_input->streams[audioIndexInput]->codecpar->channels;
    result = initSwrContext(channelCount, sampleFmt, sampleRate);
    if (result < 0) {
        LOGE(" initSwrContext faild !");
        return -1;
    }
//  videoCodecD
    if (videoCodecD == NULL) {
        LOGE(" 没找到视频解码器 ");
        return -1;
    }
    vCtxD = avcodec_alloc_context3(videoCodecD);
    if (!vCtxD) {
        LOGE("vc AVCodecContext FAILD ! ");
        return -1;
    }

    avcodec_parameters_to_context(vCtxD, afc_input->streams[videoIndexInput]->codecpar);
    result = avcodec_open2(vCtxD, videoCodecD, NULL);
    if (result < 0) {
        LOGE(" decode avcodec_open2 Faild !");
        return -1;
    }
//decoder audio
    if (audioCodecD == NULL) {
        LOGE(" audioCodecD find faild !");
        return -1;
    }
    aCtxD = avcodec_alloc_context3(audioCodecD);
    if (!aCtxD) {
        LOGE("vc AVCodecContext FAILD ! ");
        return -1;
    }
    avcodec_parameters_to_context(aCtxD, afc_input->streams[audioIndexInput]->codecpar);
    result = avcodec_open2(aCtxD, audioCodecD, NULL);
    if (result < 0) {
        LOGE(" decode avcodec_open2 Faild !");
        return -1;
    }
    LOGE(" init input success ");

    return 1;
}

int VideoJoint::initOutput(char *path) {
    int result = 0;
    afc_output = NULL;
    result = avformat_alloc_output_context2(&afc_output, NULL, NULL, path);
    if (result < 0 || afc_output == NULL) {
        LOGE(" avformat_alloc_output_context2 faild %s ", av_err2str(result));
        return -1;
    }
    afot = afc_output->oformat;
    if (addVideoOutputStream(outWidth, outHeight) < 0) {
        return -1;
    }
    if (addAudioOutputStream() < 0) {
        return -1;
    }

    if (!(afot->flags & AVFMT_NOFILE)) {
        result = avio_open(&afc_output->pb, path, AVIO_FLAG_WRITE);
        if (result < 0) {
            LOGE("Could not open output file %s ", path);
            return -1;
        }
    }

    result = avformat_write_header(afc_output, NULL);

    if (result < 0) {
        LOGE(" avformat_write_header %s", av_err2str(result));
        return -1;
    }

    outVFrame = av_frame_alloc();
    outVFrame->width = outWidth;
    outVFrame->height = outHeight;
    outVFrame->format = AV_PIX_FMT_YUV420P;
    result = av_frame_get_buffer(outVFrame, 0);
    if (result < 0) {
        LOGE(" av_frame_get_buffer FAILD ! ");
        return -1;
    }
    result = av_frame_make_writable(outVFrame);
    if (result < 0) {
        LOGE(" av_frame_make_writable FAILD ! ");
        return -1;
    }
    outAFrame = av_frame_alloc();
    outAFrame->sample_rate = sampleRate;
    outAFrame->format = sampleFormat;
    outAFrame->channel_layout = outChannelLayout;
    outAFrame->channels = channel;
    outAFrame->nb_samples = nbSample;
    result = av_frame_get_buffer(outAFrame, 0);
    if (result < 0) {
        LOGE("outAFrame av_frame_get_buffer FAILD ! %s ", av_err2str(result));
        return -1;
    }
    result = av_frame_make_writable(outAFrame);
    if (result < 0) {
        LOGE(" outAFrame av_frame_make_writable FAILD ! ");
        return -1;
    }
    LOGE(" INIT OUTPUT SUCCESS !");

    return 1;
}

int VideoJoint::addVideoOutputStream(int width, int height) {
    int result = 0;
    videoOutStream = avformat_new_stream(afc_output, NULL);
    if (videoOutStream == NULL) {
        LOGE(" VIDEO STREAM NULL ");
        return -1;
    }
    videoIndexOutput = videoOutStream->index;
    if (afot->video_codec == AV_CODEC_ID_NONE) {
        LOGE(" VIDEO AV_CODEC_ID_NONE ");
        return -1;
    }
    videoCodecE = avcodec_find_encoder(afot->video_codec);

    if (videoCodecE == NULL) {
        LOGE("VIDEO avcodec_find_encoder FAILD ! ");
        return -1;
    }
    LOGE("video ENCODE NAME %s ", videoCodecE->name);
    vCtxE = avcodec_alloc_context3(videoCodecE);

    if (vCtxE == NULL) {
        LOGE(" avcodec_alloc_context3 FAILD ! ");
        return -1;
    }

    vCtxE->bit_rate = 400000;
    vCtxE->time_base = (AVRational) {1, 25};
    vCtxE->framerate = (AVRational) {25, 1};
    vCtxE->gop_size = 10;
    vCtxE->max_b_frames = 1;
    vCtxE->pix_fmt = AV_PIX_FMT_YUV420P;
    vCtxE->codec_type = AVMEDIA_TYPE_VIDEO;
    vCtxE->width = width;
    vCtxE->height = height;

    result = avcodec_parameters_from_context(videoOutStream->codecpar, vCtxE);

    if (result < 0) {
        LOGE(" avcodec_parameters_from_context FAILD ! ");
        return -1;
    }

    result = avcodec_open2(vCtxE, videoCodecE, NULL);

    if (result < 0) {
        LOGE("video Could not open codec %s ", av_err2str(result));
        return -1;
    }

    LOGE(" INIT OUTPUT SUCCESS VIDEO !");

    return 1;
}

int VideoJoint::addAudioOutputStream() {
    int result = 0;
    audioOutStream = avformat_new_stream(afc_output, NULL);
    if (audioOutStream == NULL) {
        LOGE(" VIDEO STREAM NULL ");
        return -1;
    }
    audioIndexOutput = audioOutStream->index;
    if (afot->audio_codec == AV_CODEC_ID_NONE) {
        LOGE(" VIDEO AV_CODEC_ID_NONE ");
        return -1;
    }
    audioCodecE = avcodec_find_encoder(afot->audio_codec);
    if (audioCodecE == NULL) {
        LOGE(" audioCodecE NULL ");
        return -1;
    }
    aCtxE = avcodec_alloc_context3(audioCodecE);
    if (aCtxE == NULL) {
        LOGE("AUDIO avcodec_alloc_context3 FAILD !");
        return -1;
    }

    aCtxE->bit_rate = 64000;
    aCtxE->sample_fmt = sampleFormat;//AV_SAMPLE_FMT_S16;
    aCtxE->sample_rate = sampleRate;
    aCtxE->channel_layout = outChannelLayout;
    aCtxE->channels = channel;

    result = avcodec_parameters_from_context(audioOutStream->codecpar, aCtxE);

    if (result < 0) {
        LOGE(" avcodec_parameters_from_context FAILD ! ");
        return -1;
    }

    result = avcodec_open2(aCtxE, audioCodecE, NULL);
    if (result < 0) {
        LOGE(" audio Could not open codec %s ", av_err2str(result));
        return -1;
    }

    LOGE(" INIT OUTPUT SUCCESS AUDIO  !");
    return 1;
}

void VideoJoint::destroyInput() {
    if (vCtxD != NULL) {
        avcodec_free_context(&vCtxD);
        vCtxD = NULL;
    }
    if (aCtxD != NULL) {
        avcodec_free_context(&aCtxD);
        aCtxD = NULL;
    }
    if (afc_input != NULL) {
        avformat_free_context(afc_input);
        afc_input = NULL;
    }
}

void VideoJoint::destroyOutput() {
    if (vCtxE != NULL) {
        avcodec_free_context(&vCtxE);
        vCtxE = NULL;
    }
    if (aCtxE != NULL) {
        avcodec_free_context(&aCtxE);
        aCtxE = NULL;
    }
    if (afc_output != NULL) {
        avformat_free_context(afc_output);
        afc_output = NULL;
    }
}


VideoJoint::~VideoJoint() {
    destroyOutput();
    destroyInput();
    //还需释放其他路径等
}