/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.
*
*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#include <QApplication>
#include <QVideoFrameFormat>
#include <QtFFmpegMediaPluginImpl/private/qffmpeg_p.h>
#include <QtFFmpegMediaPluginImpl/private/qffmpegvideobuffer_p.h>
#include <QtMultimedia/private/qvideoframe_p.h>

#include <f1x/openauto/autoapp/Projection/QtVideoOutput.hpp>
#include <f1x/openauto/Common/Log.hpp>

#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libavutil/hwcontext_vaapi.h>
}

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

namespace
{

static std::string ffmpegErrorString(int error)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error, buffer, sizeof(buffer));
    return std::string(buffer);
}


static enum AVPixelFormat get_vaapi_format(
    AVCodecContext* ctx,
    const enum AVPixelFormat* pix_fmts)
{
    (void)ctx;

    for (const enum AVPixelFormat* p = pix_fmts;
         *p != AV_PIX_FMT_NONE;
         ++p)
    {
        OPENAUTO_LOG(debug)
            << "[QtVideoOutput] [HWACCEL] FFmpeg offered pixel format: "
            << av_get_pix_fmt_name(*p);

        if (*p == AV_PIX_FMT_VAAPI)
        {
            OPENAUTO_LOG(info)
                << "[QtVideoOutput] [HWACCEL] Selecting VAAPI.";

            return AV_PIX_FMT_VAAPI;
        }
    }

    OPENAUTO_LOG(warning)
        << "[QtVideoOutput] [HWACCEL] VAAPI unavailable, "
           "falling back to software decoding.";

    for (const enum AVPixelFormat* p = pix_fmts;
         *p != AV_PIX_FMT_NONE;
         ++p)
    {
        if (*p == AV_PIX_FMT_YUV420P)
        {
            OPENAUTO_LOG(info)
                << "[QtVideoOutput] [HWACCEL] Selecting software YUV420P.";

            return AV_PIX_FMT_YUV420P;
        }
    }

    return pix_fmts[0];
}

} // namespace


QtVideoOutput::QtVideoOutput(
    configuration::IConfiguration::Pointer configuration)
    : VideoOutput(std::move(configuration))
{
    this->moveToThread(QApplication::instance()->thread());

    connect(
        this,
        &QtVideoOutput::startPlayback,
        this,
        &QtVideoOutput::onStartPlayback,
        Qt::QueuedConnection);

    connect(
        this,
        &QtVideoOutput::stopPlayback,
        this,
        &QtVideoOutput::onStopPlayback,
        Qt::QueuedConnection);

    QMetaObject::invokeMethod(
        this,
        "createVideoOutput",
        Qt::BlockingQueuedConnection);
}


QtVideoOutput::~QtVideoOutput()
{
    cleanupDecoder();
}


void QtVideoOutput::createVideoOutput()
{
    OPENAUTO_LOG(debug)
        << "[QtVideoOutput] create.";

    videoWidget_ = std::make_unique<QVideoWidget>();
    videoSink_ = videoWidget_->videoSink();
}

void QtVideoOutput::initDecoder()
{
    std::lock_guard<std::mutex> lock(decoderMutex_);

    if (codecContext_)
        return;

    parser_ = av_parser_init(AV_CODEC_ID_H264);

    if (!parser_)
    {
        OPENAUTO_LOG(error)
            << "[QtVideoOutput] [Parser] Failed to create H.264 parser.";

        codec_ = nullptr;
        return;
    }

	codec_ = avcodec_find_decoder(AV_CODEC_ID_H264);

	if (!codec_)
	{
		OPENAUTO_LOG(error)
			<< "[QtVideoOutput] [Decoder] "
			   "H.264 decoder not found.";

		av_parser_close(parser_);
		parser_ = nullptr;
		return;
	}

    codecContext_ = avcodec_alloc_context3(codec_);

    if (!codecContext_)
    {
        OPENAUTO_LOG(error)
            << "[QtVideoOutput] [Decoder] Failed to allocate codec context.";

        av_parser_close(parser_);
        parser_ = nullptr;
        codec_ = nullptr;

        return;
    }

    int ret = av_hwdevice_ctx_create(
        &hwDeviceContext_,
        AV_HWDEVICE_TYPE_VAAPI,
        "/dev/dri/renderD128",
        nullptr,
        0);

    if (ret < 0)
    {
        OPENAUTO_LOG(warning)
            << "[QtVideoOutput] [HWACCEL] "
               "av_hwdevice_ctx_create failed: "
            << ffmpegErrorString(ret)
            << "; using software decoding.";
    }
    else
    {
        OPENAUTO_LOG(info)
            << "[QtVideoOutput] [HWACCEL] VAAPI device created.";

        codecContext_->hw_device_ctx =
            av_buffer_ref(hwDeviceContext_);

        if (!codecContext_->hw_device_ctx)
        {
            OPENAUTO_LOG(warning)
                << "[QtVideoOutput] [HWACCEL] Failed to reference VAAPI device; "
                   "using software decoding.";
            av_buffer_unref(&hwDeviceContext_);
        }
        else
        {
            codecContext_->get_format = get_vaapi_format;
            codecContext_->hwaccel_flags |=
                AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH;
        }
    }

    ret = avcodec_open2(
        codecContext_,
        codec_,
        nullptr);

    if (ret < 0)
    {
        OPENAUTO_LOG(error)
            << "[QtVideoOutput] [Decoder] "
               "avcodec_open2 failed: "
            << ffmpegErrorString(ret);

        cleanupDecoderUnlocked();
        return;
    }

    OPENAUTO_LOG(info)
        << "[QtVideoOutput] [Decoder] "
           "H.264 decoder opened.";

    packet_ = av_packet_alloc();

    if (!packet_)
    {
        OPENAUTO_LOG(error)
            << "[QtVideoOutput] [Decoder] "
               "Failed to allocate AVPacket.";

        cleanupDecoderUnlocked();
        return;
    }

    frame_ = av_frame_alloc();

    if (!frame_)
    {
        OPENAUTO_LOG(error)
            << "[QtVideoOutput] [Decoder] "
               "Failed to allocate AVFrame.";

        cleanupDecoderUnlocked();
        return;
    }


    OPENAUTO_LOG(info)
        << "[QtVideoOutput] Decoder initialization complete ("
        << (hwDeviceContext_ ? "VAAPI" : "software")
        << ").";
}

void QtVideoOutput::cleanupDecoder()
{
    std::lock_guard<std::mutex> lock(decoderMutex_);
    cleanupDecoderUnlocked();
}


void QtVideoOutput::cleanupDecoderUnlocked()
{

    if (codecContext_ && frame_)
    {
        avcodec_send_packet(codecContext_, nullptr);

        while (avcodec_receive_frame(codecContext_, frame_) == 0)
        {
            processDecodedFrame(frame_);
        }
    }

    if (swsContext_)
    {
        sws_freeContext(swsContext_);
        swsContext_ = nullptr;
    }

    if (frame_)
    {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }

    if (softwareFrame_)
    {
        av_frame_free(&softwareFrame_);
        softwareFrame_ = nullptr;
    }

    if (packet_)
    {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }

    if (codecContext_)
    {
        avcodec_free_context(&codecContext_);
        codecContext_ = nullptr;
    }

    if (parser_)
    {
        av_parser_close(parser_);
        parser_ = nullptr;
    }

    if (hwDeviceContext_)
    {
        av_buffer_unref(&hwDeviceContext_);
        hwDeviceContext_ = nullptr;
    }

    codec_ = nullptr;
}


bool QtVideoOutput::open()
{
    initDecoder();

    return codecContext_ != nullptr;
}


bool QtVideoOutput::init()
{
    emit startPlayback();

    return true;
}


void QtVideoOutput::stop()
{
    emit stopPlayback();
}


void QtVideoOutput::write(
    uint64_t,
    const aasdk::common::DataConstBuffer& buffer)
{
	std::lock_guard<std::mutex> lock(decoderMutex_);
    if (!codecContext_ ||
        !parser_ ||
        !packet_ ||
        !frame_)
    {
        return;
    }

    const uint8_t* data = buffer.cdata;
    int dataSize = static_cast<int>(buffer.size);

    while (dataSize > 0)
    {
        int ret = av_parser_parse2(
            parser_,
            codecContext_,
            &packet_->data,
            &packet_->size,
            data,
            dataSize,
            AV_NOPTS_VALUE,
            AV_NOPTS_VALUE,
            0);

        if (ret < 0)
        {
            OPENAUTO_LOG(error)
                << "[QtVideoOutput] [Parser] av_parser_parse2 failed: "
                << ffmpegErrorString(ret);

            break;
        }

        data += ret;
        dataSize -= ret;

        if (packet_->size <= 0)
        {
            continue;
        }

        ret = avcodec_send_packet(
            codecContext_,
            packet_);

        if (ret < 0)
        {
            if (ret == AVERROR(EAGAIN))
            {
                OPENAUTO_LOG(warning)
                    << "[QtVideoOutput] [Decoder] "
                       "avcodec_send_packet returned EAGAIN.";

                continue;
            }

            OPENAUTO_LOG(error)
                << "[QtVideoOutput] [Decoder] "
                   "avcodec_send_packet failed: "
                << ffmpegErrorString(ret);

            continue;
        }

        while (true)
        {
            ret = avcodec_receive_frame(
                codecContext_,
                frame_);

            if (ret == 0)
            {
                processDecodedFrame(frame_);
                continue;
            }

            if (ret == AVERROR(EAGAIN) ||
                ret == AVERROR_EOF)
            {
                break;
            }

            OPENAUTO_LOG(error)
                << "[QtVideoOutput] [Decoder] "
                   "avcodec_receive_frame failed: "
                << ffmpegErrorString(ret);

            break;
        }
    }
}


void QtVideoOutput::processDecodedFrame(AVFrame* frame)
{
    if (!frame ||
        frame->width <= 0 ||
        frame->height <= 0)
    {
        return;
    }

    const AVPixelFormat inputFormat =
        static_cast<AVPixelFormat>(frame->format);

    if (inputFormat == AV_PIX_FMT_VAAPI)
    {
        // QFFmpegVideoBuffer is Qt Multimedia's native VA-API/RHI bridge.
        // Keeping the AVFrame as a hardware frame avoids vaGetImage() and
        // the GPU-to-CPU transfer that the old QVideoFrame path required.
        auto qtFrame = QFFmpeg::makeAVFrame();
        if (!qtFrame || av_frame_ref(qtFrame.get(), frame) < 0)
        {
            OPENAUTO_LOG(error)
                << "[QtVideoOutput] [HWACCEL] Failed to reference VAAPI frame.";
            return;
        }

        auto videoBuffer =
            std::make_unique<QFFmpegVideoBuffer>(std::move(qtFrame));
        const QVideoFrameFormat format(
            videoBuffer->size(), videoBuffer->pixelFormat());
        QVideoFrame qframe = QVideoFramePrivate::createFrame(
            std::move(videoBuffer), format);

        if (!qframe.isValid())
        {
            OPENAUTO_LOG(error)
                << "[QtVideoOutput] [HWACCEL] Failed to create native RHI frame.";
            return;
        }

        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            latestFrame_ = std::move(qframe);

            if (!hasPendingFrame_)
            {
                hasPendingFrame_ = true;
                QMetaObject::invokeMethod(
                    this,
                    "presentLatestFrame",
                    Qt::QueuedConnection);
            }
        }
        return;
    }

    AVFrame* softwareFrame = nullptr;
    AVFrame* sourceFrame = frame;

    const AVPixelFormat sourceFormat =
        static_cast<AVPixelFormat>(sourceFrame->format);
    const bool sourceIsNV12 = sourceFormat == AV_PIX_FMT_NV12;
    const auto qtPixelFormat = sourceIsNV12
        ? QVideoFrameFormat::Format_NV12
        : QVideoFrameFormat::Format_YUV420P;

    QVideoFrameFormat format(
        QSize(sourceFrame->width, sourceFrame->height),
        qtPixelFormat);

    QVideoFrame qframe(format);

    if (!qframe.map(QVideoFrame::WriteOnly))
    {
        OPENAUTO_LOG(error)
            << "[QtVideoOutput] Failed to map QVideoFrame";

        if (softwareFrame != softwareFrame_)
        {
            av_frame_free(&softwareFrame);
        }
        return;
    }


    uint8_t* dstData[4] = {
        qframe.bits(0),
        qframe.bits(1),
        qframe.bits(2),
        nullptr
    };

    int dstLinesizes[4] = {
        qframe.bytesPerLine(0),
        qframe.bytesPerLine(1),
        qframe.bytesPerLine(2),
        0
    };


    if (sourceFormat == AV_PIX_FMT_YUV420P ||
        sourceFormat == AV_PIX_FMT_YUVJ420P ||
        sourceFormat == AV_PIX_FMT_NV12)
    {
        av_image_copy(
            dstData,
            dstLinesizes,
            const_cast<const uint8_t**>(sourceFrame->data),
            sourceFrame->linesize,
            sourceIsNV12 ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P,
            sourceFrame->width,
            sourceFrame->height);
    }
    else
    {
        swsContext_ = sws_getCachedContext(
            swsContext_,
            sourceFrame->width,
            sourceFrame->height,
            sourceFormat,
            sourceFrame->width,
            sourceFrame->height,
            AV_PIX_FMT_YUV420P,
            SWS_FAST_BILINEAR,
            nullptr,
            nullptr,
            nullptr);

        if (!swsContext_)
        {
            qframe.unmap();

            if (softwareFrame != softwareFrame_)
            {
                av_frame_free(&softwareFrame);
            }

            OPENAUTO_LOG(error)
                << "[QtVideoOutput] Failed to create swscale context.";

            return;
        }

        const int scaledHeight = sws_scale(
            swsContext_,
            sourceFrame->data,
            sourceFrame->linesize,
            0,
            sourceFrame->height,
            dstData,
            dstLinesizes);

        if (scaledHeight <= 0)
        {
            qframe.unmap();

            if (softwareFrame != softwareFrame_)
            {
                av_frame_free(&softwareFrame);
            }

            OPENAUTO_LOG(error)
                << "[QtVideoOutput] sws_scale failed.";

            return;
        }
    }

    qframe.unmap();

    if (softwareFrame != softwareFrame_)
    {
        av_frame_free(&softwareFrame);
    }

    {
        std::lock_guard<std::mutex> lock(frameMutex_);

        latestFrame_ = std::move(qframe);

        if (!hasPendingFrame_)
        {
            hasPendingFrame_ = true;

            QMetaObject::invokeMethod(
                this,
                "presentLatestFrame",
                Qt::QueuedConnection);
        }
    }
}


void QtVideoOutput::presentLatestFrame()
{
    QVideoFrame frame;

    {
        std::lock_guard<std::mutex> lock(frameMutex_);

        hasPendingFrame_ = false;

        frame = std::move(latestFrame_);
    }

    if (videoSink_ && frame.isValid())
    {
        videoSink_->setVideoFrame(frame);
    }
}


void QtVideoOutput::onStartPlayback()
{
    videoWidget_->setAspectRatioMode(
        Qt::IgnoreAspectRatio);

    videoWidget_->setFocus();

    videoWidget_->setWindowFlags(
        Qt::WindowStaysOnTopHint);

    videoWidget_->setFullScreen(true);

    videoWidget_->show();

    videoSink_ = videoWidget_->videoSink();
}


void QtVideoOutput::onStopPlayback()
{
    videoWidget_->hide();

    videoSink_ = nullptr;
}

}
}
}
}
