#include <raylib.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include <iostream>

#include <chrono>
#include <thread>

struct VideoContext {
    AVFormatContext* pFormatCtx;
    AVCodecContext* pCodecCtx;
    AVFrame* pFrame;
    struct SwsContext* sws_ctx;
    int videoStream;
    double videoFrameRate;
    double frameDelay;
    double currentTime;
    RenderTexture2D frameTexture;
    Image frameImage;
    Texture2D videoTexture;
    bool finished = false;
    bool loop = true;
};

VideoContext InitializeVideoContext(const char* videoFile) {
    VideoContext videoContext;

    av_register_all();
    avcodec_register_all();

    videoContext.pFormatCtx = NULL;
    if (avformat_open_input(&videoContext.pFormatCtx, videoFile, NULL, NULL) != 0) {
        TraceLog(LOG_ERROR, "Error opening video file");
        return videoContext;
    }

    if (avformat_find_stream_info(videoContext.pFormatCtx, NULL) < 0) {
        TraceLog(LOG_ERROR, "Error finding stream information");
        return videoContext;
    }

    videoContext.videoStream = -1;
    videoContext.pCodecCtx = NULL;
    for (int i = 0; i < videoContext.pFormatCtx->nb_streams; i++) {
        if (videoContext.pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoContext.videoStream = i;
            videoContext.pCodecCtx = avcodec_alloc_context3(avcodec_find_decoder(videoContext.pFormatCtx->streams[i]->codecpar->codec_id));
            if (videoContext.pCodecCtx == NULL) {
                TraceLog(LOG_ERROR, "Error allocating codec context");
                return videoContext;
            }
            if (avcodec_parameters_to_context(videoContext.pCodecCtx, videoContext.pFormatCtx->streams[i]->codecpar) < 0) {
                TraceLog(LOG_ERROR, "Failed to copy codec parameters to codec context");
                return videoContext;
            }
            if (avcodec_open2(videoContext.pCodecCtx, avcodec_find_decoder(videoContext.pFormatCtx->streams[i]->codecpar->codec_id), NULL) < 0) {
                TraceLog(LOG_ERROR, "Could not open codec");
                return videoContext;
            }
            break;
        }
    }

    videoContext.pFrame = av_frame_alloc();
    videoContext.sws_ctx = NULL;
    videoContext.videoFrameRate = av_q2d(videoContext.pFormatCtx->streams[videoContext.videoStream]->avg_frame_rate);
    videoContext.frameDelay = 1.0 / videoContext.videoFrameRate;
    videoContext.currentTime = 0.0;
    videoContext.frameTexture = LoadRenderTexture(videoContext.pCodecCtx->width, videoContext.pCodecCtx->height);
    videoContext.frameImage = GenImageColor(videoContext.pCodecCtx->width, videoContext.pCodecCtx->height, BLANK);
    videoContext.videoTexture = LoadTextureFromImage(videoContext.frameImage);

    return videoContext;
}

VideoContext UpdateVideoContext(VideoContext& videoContext) {
    AVPacket packet;
    double targetFrameTime = 1.0 / videoContext.videoFrameRate; // Target time per video frame

    if (av_read_frame(videoContext.pFormatCtx, &packet) < 0) {
        if (!videoContext.loop) {
            TraceLog(LOG_INFO, "End of video file");
            videoContext.finished = true;
            return videoContext;  // Return early in case of error
        } else {
            std::cout << "Looping" << std::endl;
            videoContext.frameDelay = targetFrameTime; // Use target frame time
            videoContext.finished = false;
            av_seek_frame(videoContext.pFormatCtx, videoContext.videoStream, 0, AVSEEK_FLAG_FRAME);
            return videoContext;  // Start playing the video from the beginning
        }
    }

    if (packet.stream_index == videoContext.videoStream) {
        avcodec_send_packet(videoContext.pCodecCtx, &packet);
        if (avcodec_receive_frame(videoContext.pCodecCtx, videoContext.pFrame) == 0) {
            if (!videoContext.sws_ctx) {
                videoContext.sws_ctx = sws_getContext(videoContext.pCodecCtx->width, videoContext.pCodecCtx->height, videoContext.pCodecCtx->pix_fmt,
                                            videoContext.pCodecCtx->width, videoContext.pCodecCtx->height, AV_PIX_FMT_RGBA,
                                            SWS_BILINEAR, NULL, NULL, NULL);
                if (!videoContext.sws_ctx) {
                    TraceLog(LOG_ERROR, "Failed to create SwsContext");
                    return videoContext;  // Return early in case of error
                }
            }

            uint8_t *pixels[1] = {(uint8_t *)videoContext.frameImage.data};
            int pitch[1] = {4 * videoContext.pCodecCtx->width};
            sws_scale(videoContext.sws_ctx, videoContext.pFrame->data, videoContext.pFrame->linesize, 0, videoContext.pCodecCtx->height,
                        pixels, pitch);

            UpdateTexture(videoContext.videoTexture, videoContext.frameImage.data);

            videoContext.currentTime += videoContext.frameDelay;

            // Calculate time to sleep based on target frame time and GetFrameTime()
            double timeToSleep = targetFrameTime - GetFrameTime();
            if (timeToSleep > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(timeToSleep * 1000))); // Sleep in milliseconds
            }
        }
    }

    av_packet_unref(&packet);

    return videoContext;
}


void UninitializeVideoContext(VideoContext& videoContext) {
    av_frame_free(&videoContext.pFrame);
    avcodec_close(videoContext.pCodecCtx);
    avformat_close_input(&videoContext.pFormatCtx);
    sws_freeContext(videoContext.sws_ctx);

    UnloadRenderTexture(videoContext.frameTexture);
    UnloadImage(videoContext.frameImage);
    UnloadTexture(videoContext.videoTexture);
}

int main() {
    // Initialize Raylib
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(100, 100, "Video Playback");

    VideoContext videoContext = InitializeVideoContext("abc.avi");
    if (!videoContext.pCodecCtx) {
        CloseWindow();
        return -1;
    }


    while (!WindowShouldClose()) {
        videoContext = UpdateVideoContext(videoContext);

        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawTextureRec(videoContext.videoTexture, (Rectangle){0, 0, (float)videoContext.pCodecCtx->width, (float)videoContext.pCodecCtx->height},
                        (Vector2){0, 0}, WHITE);

        DrawFPS(GetScreenWidth() * .9, GetScreenHeight() * .1);

        EndDrawing();
    }

    UninitializeVideoContext(videoContext);

    CloseWindow();

    return 0;
}
