class Video {
private:
    AVStream* videoStream = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVCodecParameters* videoPar = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* pRGBFrame = nullptr;
    struct SwsContext* sws_ctx = nullptr;
    AVFormatContext* formatCtx = nullptr;
    Texture2D texture = { 0 };
    std::chrono::_V2::system_clock::time_point currentTime;
    std::chrono::_V2::system_clock::time_point lastFrameTime;
    double elapsedTime;
    double frameDuration;

public:
    void openVideo(const char* filename) noexcept {
        formatCtx = avformat_alloc_context();
        if (avformat_open_input(&formatCtx, filename, nullptr, nullptr) != 0) {
            std::cerr << "Failed to open video file" << std::endl;
            return;
        }
        if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
            std::cerr << "Failed to find stream info" << std::endl;
            avformat_close_input(&formatCtx);
            return;
        }

        for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
            AVCodecParameters* tmpPar = formatCtx->streams[i]->codecpar;
            if (tmpPar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStream = formatCtx->streams[i];
                videoPar = tmpPar;
                break;
            }
        }

        if (!videoStream) {
            std::cerr << "Could not find video stream" << std::endl;
            avformat_close_input(&formatCtx);
            return;
        }

        const AVCodec* videoCodec = avcodec_find_decoder(videoPar->codec_id);
        if (!videoCodec) {
            std::cerr << "Unsupported codec" << std::endl;
            avformat_close_input(&formatCtx);
            return;
        }

        codecCtx = avcodec_alloc_context3(videoCodec);
        if (!codecCtx) {
            std::cerr << "Failed to allocate codec context" << std::endl;
            avformat_close_input(&formatCtx);
            return;
        }

        if (avcodec_parameters_to_context(codecCtx, videoPar) < 0) {
            std::cerr << "Failed to copy codec parameters to context" << std::endl;
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            return;
        }

        if (avcodec_open2(codecCtx, videoCodec, nullptr) < 0) {
            std::cerr << "Failed to open codec" << std::endl;
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            return;
        }


        // Allocate frames
        frame = av_frame_alloc();
        pRGBFrame = av_frame_alloc();
        if (!frame || !pRGBFrame) {
            std::cerr << "Failed to allocate frames" << std::endl;
            cleanup();
            return;
        }

        pRGBFrame->format = AV_PIX_FMT_RGB24;
        pRGBFrame->width = codecCtx->width;
        pRGBFrame->height = codecCtx->height;
        if (av_frame_get_buffer(pRGBFrame, 0) < 0) {
            std::cerr << "Failed to allocate RGB frame buffer" << std::endl;
            cleanup();
            return;
        }

        // Initialize SwsContext for converting to RGB format
        sws_ctx = sws_getContext(codecCtx->width, codecCtx->height, codecCtx->pix_fmt, codecCtx->width, codecCtx->height, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_ctx) {
            std::cerr << "Could not initialize the conversion context" << std::endl;
            cleanup();
            return;
        }

        // Allocate Raylib texture
        texture.height = codecCtx->height;
        texture.width = codecCtx->width;
        texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
        texture.mipmaps = 1;
        texture.id = rlLoadTexture(nullptr, texture.width, texture.height, texture.format, texture.mipmaps);

        if (texture.id == 0) {
            std::cerr << "Failed to load texture" << std::endl;
            cleanup();
            return;
        }

        lastFrameTime = std::chrono::high_resolution_clock::now();
        frameDuration = 1.0 / (double)(videoStream->avg_frame_rate.num / videoStream->avg_frame_rate.den);
    }

    void processVideoFrame() noexcept {
        if (!codecCtx || !frame || !pRGBFrame || !sws_ctx) {
            TraceLog(LOG_WARNING, "Video not initialized");
            return;
        }

        int ret = avcodec_receive_frame(codecCtx, frame);
        if (ret >= 0) {
            sws_scale(sws_ctx, (uint8_t const* const*)frame->data, frame->linesize, 0, frame->height, pRGBFrame->data, pRGBFrame->linesize);
            UpdateTexture(texture, pRGBFrame->data[0]);
        } else if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            std::cerr << "Error receiving frame" << std::endl;
        }
    }

    void cleanup() noexcept {
        avformat_close_input(&this->formatCtx);
        avcodec_free_context(&this->codecCtx);
        av_frame_free(&this->frame);
        av_frame_free(&this->pRGBFrame);
        sws_freeContext(this->sws_ctx);
        UnloadTexture(this->texture);
    }

    void PlayCurrentFrame() noexcept {
        currentTime = std::chrono::high_resolution_clock::now();
        elapsedTime = std::chrono::duration<double>(currentTime - lastFrameTime).count();

        if (elapsedTime >= frameDuration) {
            lastFrameTime = currentTime;

            AVPacket packet;

            int readFrameResult = av_read_frame(formatCtx, &packet);
            if (readFrameResult < 0) {
                if (readFrameResult == AVERROR_EOF) {
                    av_seek_frame(formatCtx, videoStream->index, 0, AVSEEK_FLAG_FRAME); // Loop
                    return;
                } else {
                    std::cerr << "Error reading frame" << std::endl;
                    return;
                }
            }

            if (packet.stream_index == videoStream->index) {
                int ret = avcodec_send_packet(codecCtx, &packet);

                if (ret >= 0) {
                    processVideoFrame();
                } else {
                    std::cerr << "Error sending packet" << std::endl;
                }
            }
            
            av_packet_unref(&packet);
        }
    }

    Texture2D& getTexture() noexcept {
        return texture;
    }

    bool hasVideoStream() const {
        return videoStream != nullptr;
    }
};
