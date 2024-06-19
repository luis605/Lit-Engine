class VideoPlayer {
public:
    VideoPlayer(const char* videoFile) {
        if (!videoFile) {
            TraceLog(LOG_ERROR, "No video file provided");
            return;
        }

        av_init_packet(&packet);

    	int res = avformat_open_input(&pFormatCtx, videoFile, NULL, NULL);
	    if (res != 0) {
	        TraceLog(LOG_ERROR, "Could not open video file");
	        return;
	    }

        res = avformat_find_stream_info(pFormatCtx, NULL);
        if (res < 0) {
            TraceLog(LOG_ERROR, "Could not find stream information");
            return;
        }

        videoStream = get_video_stream();
        if (videoStream == -1) {
            TraceLog(LOG_ERROR, "Could not find video stream. AVCodecParameters probably does not have codecpar_type type AVMEDIA_TYPE_VIDEO");
            return;
        }

    	pCodec = avcodec_find_decoder(pCodecParameters->codec_id);
        if (pCodec == NULL) {
            TraceLog(LOG_ERROR, "Video decoder not found");
            return;
        }

        pCodecCtx = avcodec_alloc_context3(pCodec);
        if(pCodecCtx == NULL) {
            TraceLog(LOG_ERROR, "Failed to allocate video context decoder");
            return;
        }

        res = avcodec_parameters_to_context(pCodecCtx, pCodecParameters);

        if(res < 0) {
            TraceLog(LOG_ERROR, "Failed to transfer video parameters to context");
            return;
        }
   
    	pFrame = av_frame_alloc();
		if (pFrame == NULL) {
            TraceLog(LOG_ERROR, "Could not allocate frame memory");
            return;
        }

        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height,1);

        buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

        res = av_image_fill_arrays(pFrame->data, pFrame->linesize, buffer, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);
        if (res < 0) {
            TraceLog(LOG_ERROR, "Could not allocate image data");
            return;
        }

        if (pFormatCtx->streams[video_stream_index]->r_frame_rate.den != 0) {
            frameRate = static_cast<float>(pFormatCtx->streams[video_stream_index]->r_frame_rate.num) /
                        static_cast<float>(pFormatCtx->streams[video_stream_index]->r_frame_rate.den);
        } else {
            frameRate = 30.0f; // Set a default frame rate
        }

        sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                                 pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGBA,
                                 SWS_BILINEAR, NULL, NULL, NULL);

        if (!sws_ctx) {
            TraceLog(LOG_ERROR, "Failed to create SwsContext");
            Cleanup();
            return;
        }

        frameImage = GenImageColor(pCodecCtx->width, pCodecCtx->height, BLANK);
        videoTexture = LoadTextureFromImage(frameImage);
        frameDelay = 1.0f / frameRate;
        lastFrameTime = GetTime();
        currentTime = 0.0f;
        finished = false;
        loop = true;

        TraceLog(LOG_INFO, "VideoPlayer initialized successfully");
    }


    int get_video_stream() {
        int videoStream = -1;

        for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++){
            if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStream = i;
                break;
            }
        }

        if (videoStream == -1) {
            TraceLog(LOG_ERROR, "Could not find video stream");
            return -1;
        }

        pCodecParameters = pFormatCtx->streams[videoStream]->codecpar;
        video_stream_index = videoStream;
        return videoStream;
    }
    ~VideoPlayer() {
        Cleanup();
    }

    void ProcessFrame() {
        float frameTime = GetTime() - lastFrameTime;

        if (frameTime < frameDelay) {
            return;
        }

        uint8_t *pixels[1] = { reinterpret_cast<uint8_t*>(frameImage.data) };
        int pitch[1] = { 4 * pCodecCtx->width };

        sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
                  pixels, pitch);

        UpdateTexture(videoTexture, frameImage.data);

        currentTime += frameDelay;
        lastFrameTime = GetTime();
    }

    void Update() {
        if (!pFormatCtx || !pCodecCtx || !pFrame)
            return;

        int readFrameResult = av_read_frame(pFormatCtx, &packet);

        if (readFrameResult >= 0 && packet.stream_index == video_stream_index) {
            avcodec_send_packet(pCodecCtx, &packet);

            if (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                ProcessFrame();
            }
        } else if (readFrameResult < 0) {
            if (!loop) {
                TraceLog(LOG_INFO, "End of video file");
                finished = true;
                return;
            }

            finished = false;
            av_seek_frame(pFormatCtx, video_stream_index, 0, AVSEEK_FLAG_FRAME);
        }
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
    void Cleanup() {
        // av_frame_free(&pFrame);
        // avcodec_free_context(&pCodecCtx);
        // sws_freeContext(sws_ctx);
        // avformat_close_input(&pFormatCtx);
        // if (frameImage.data) {
        //     free(frameImage.data);
        //     frameImage.data = NULL;
        // }
    }

private:
    AVPacket packet;
    AVFormatContext* pFormatCtx         = NULL;
	AVCodec* pCodec                     = NULL;
    AVCodecContext* pCodecCtx           = NULL;
	AVCodecParameters* pCodecParameters = NULL;
    AVFrame* pFrame                     = NULL;
    struct SwsContext* sws_ctx          = NULL;
    uint8_t* buffer                     = NULL;
    Image frameImage;
    Texture2D videoTexture;
    float frameRate;
    float frameDelay;
    float currentTime;
    float lastFrameTime;
    bool finished;
    bool loop;
    int video_stream_index = -1;
	int videoStream = 0;
};
