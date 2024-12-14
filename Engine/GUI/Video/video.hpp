#ifndef VIDEO_H
#define VIDEO_H

class Video {
private:
    AVStream* videoStream       = nullptr;
    AVFormatContext* formatCtx  = nullptr;
    AVCodecContext* codecCtx    = nullptr;
    AVCodecParameters* videoPar = nullptr;
    AVFrame* frame              = nullptr;
    AVFrame* pRGBFrame          = nullptr;
    struct SwsContext* sws_ctx  = nullptr;
    Texture2D texture           = { 0 };
    std::chrono::time_point<std::chrono::high_resolution_clock> currentTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastFrameTime;
    double elapsedTime;
    double frameDuration;

public:
    void openVideo(const char* filename) noexcept;
    void processVideoFrame() noexcept;
    void cleanup() noexcept;
    void PlayCurrentFrame() noexcept;
    Texture2D& getTexture() noexcept;
    bool hasVideoStream() const;
};

#endif // VIDEO_H