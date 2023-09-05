#include <raylib.h>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <thread>
#include <variant>

class VideoPlayer {
public:
    VideoPlayer(const char* videoFile, float frameRate = 30.0f) : frameRate(frameRate) {
        av_register_all();
        avcodec_register_all();

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
                AVCodec* codec = avcodec_find_decoder(pFormatCtx->streams[i]->codecpar->codec_id);
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

        frameImage = GenImageColor(pCodecCtx->width, pCodecCtx->height, BLANK);
        videoTexture = LoadTextureFromImage(frameImage);
        frameDelay = 1.0f / frameRate;
        currentTime = 0.0f;
        finished = false;
        loop = true;
    }

    ~VideoPlayer() {
        av_frame_free(&pFrame);
        avcodec_close(pCodecCtx);
        avformat_close_input(&pFormatCtx);
        sws_freeContext(sws_ctx);
        UnloadImage(frameImage);
        UnloadTexture(videoTexture);
    }

    void Update() {
        AVPacket packet;
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
                uint8_t *pixels[1] = {(uint8_t *)frameImage.data};
                int pitch[1] = {4 * pCodecCtx->width};
                sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
                            pixels, pitch);

                UpdateTexture(videoTexture, frameImage.data);
                currentTime += frameDelay;

                // Calculate time to sleep based on frameDelay and actual frame time
                float frameTime = GetTime() - lastFrameTime;
                float timeToSleep = frameDelay - frameTime;
                if (timeToSleep > 0.0f) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(timeToSleep * 1000)));
                }

                lastFrameTime = GetTime();
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
    float lastFrameTime = 0.0f;
};


int main() {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 600, "Video Playback");

    std::variant<Texture2D, std::unique_ptr<VideoPlayer>> video;
    video = std::make_unique<VideoPlayer>("project/game/abc.avi", 30.0f);


    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);

    if (auto* videoPlayerPtr = std::get_if<std::unique_ptr<VideoPlayer>>(&video)) {
        // Use videoPlayerPtr to access the VideoPlayer object
        (*videoPlayerPtr)->Update();
        DrawTexture((*videoPlayerPtr)->GetTexture(), 0, 0, WHITE);
        if ((*videoPlayerPtr)->IsFinished()) {
            (*videoPlayerPtr)->SetLoop(true);
        }
    }


        DrawFPS(GetScreenWidth() * 0.9, GetScreenHeight() * 0.1);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
