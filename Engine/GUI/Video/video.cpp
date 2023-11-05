#include "../../../include_all.h"

class VideoPlayer {
public:
    VideoPlayer(const char* videoFile) {
        //av_register_all();
        // avcodec_register_all();

        pFormatCtx = avformat_alloc_context();
        if (avformat_open_input(&pFormatCtx, videoFile, NULL, NULL) != 0) {
            TraceLog(LOG_ERROR, "Error opening video file");
            return;
        }

        if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
            TraceLog(LOG_ERROR, "Error finding stream information");
            return;
        }

        videoStream = -1;
        pCodecCtx = NULL;
        for (int i = 0; i < pFormatCtx->nb_streams; i++) {
            if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStream = i;
                const AVCodec* codec = avcodec_find_decoder(pFormatCtx->streams[i]->codecpar->codec_id);
                pCodecCtx = avcodec_alloc_context3(codec);
                if (pCodecCtx == NULL) {
                    TraceLog(LOG_ERROR, "Error allocating codec context");
                    return;
                }
                if (avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[i]->codecpar) < 0) {
                    TraceLog(LOG_ERROR, "Failed to copy codec parameters to codec context");
                    return;
                }
                if (avcodec_open2(pCodecCtx, codec, NULL) < 0) {
                    TraceLog(LOG_ERROR, "Could not open codec");
                    return;
                }
                break;
            }
        }

        pFrame = av_frame_alloc();
        sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                                pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, NULL, NULL, NULL);
        if (!sws_ctx) {
            TraceLog(LOG_ERROR, "Failed to create SwsContext");
            return;
        }

        if (pFormatCtx->streams[videoStream]->r_frame_rate.den != 0) {
            frameRate = static_cast<float>(pFormatCtx->streams[videoStream]->r_frame_rate.num) /
                            static_cast<float>(pFormatCtx->streams[videoStream]->r_frame_rate.den);
        } else {
            // Handle the case where the denominator is 0 (to avoid division by zero)
            frameRate = 30.0f; // Set a default frame rate
        }

        frameImage = GenImageColor(pCodecCtx->width, pCodecCtx->height, BLANK);
        videoTexture = LoadTextureFromImage(frameImage);
        frameDelay = 1.0f / frameRate;
        lastFrameTime = 0.0f;
        currentTime = 0.0f;
        finished = false;
        loop = true;
    }

    // ~VideoPlayer() {
    //     av_frame_free(&pFrame);
    //     avcodec_close(pCodecCtx);
    //     avformat_close_input(&pFormatCtx);
    //     sws_freeContext(sws_ctx);
    //     UnloadImage(frameImage);
    //     UnloadTexture(videoTexture);
    // }

    void Update() {
        AVPacket packet;
        av_init_packet(&packet);
        if (av_read_frame(pFormatCtx, &packet) < 0) {
            if (!loop) {
                TraceLog(LOG_INFO, "End of video file");
                finished = true;
                return;
            } else {
                TraceLog(LOG_INFO, "Looping");
                finished = false;
                av_seek_frame(pFormatCtx, videoStream, 0, AVSEEK_FLAG_FRAME);
                return;
            }
        }

        if (packet.stream_index == videoStream) {
            avcodec_send_packet(pCodecCtx, &packet);
            if (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                float frameTime = GetTime() - lastFrameTime;
                if (frameTime >= frameDelay) {
                    uint8_t *pixels[1] = {(uint8_t *)frameImage.data};
                    int pitch[1] = {4 * pCodecCtx->width};

                    // Ensure that the input and output dimensions match
                    int scaledWidth = pCodecCtx->width;
                    int scaledHeight = pCodecCtx->height;

                    // Perform the image scaling
                    sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
                                pixels, pitch);

                    // Update the texture with the scaled image data
                    UpdateTexture(videoTexture, frameImage.data);

                    currentTime += frameDelay;
                    lastFrameTime = GetTime();
                }
            }
        }

        av_packet_unref(&packet);
    }



    bool IsFinished() const {
        return finished;
    }

    void SetLoop(bool shouldLoop) {
        loop = shouldLoop;
    }

    Texture2D GetTexture() const {
        return videoTexture;
    }

    float GetFrameRate() const {
        return frameRate;
    }


private:
    AVFormatContext* pFormatCtx;
    AVCodecContext* pCodecCtx;
    AVFrame* pFrame;
    struct SwsContext* sws_ctx;
    int videoStream;
    Image frameImage;
    Texture2D videoTexture;
    float frameRate;
    float frameDelay;
    float currentTime;
    bool finished;
    bool loop;
    float lastFrameTime = 30.0f;
};