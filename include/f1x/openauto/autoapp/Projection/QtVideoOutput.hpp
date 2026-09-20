/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <memory>
#include <mutex>
#include <QVideoWidget>
#include <QVideoSink>
#include <QVideoFrame>
#include <boost/noncopyable.hpp>
#include <f1x/openauto/autoapp/Projection/VideoOutput.hpp>
extern "C"
{
#include <libavutil/buffer.h>
}

struct AVCodec;
struct AVCodecParserContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;

struct AVBufferRef;

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

class QtVideoOutput: public QObject, public VideoOutput, boost::noncopyable
{
    Q_OBJECT

public:
    QtVideoOutput(configuration::IConfiguration::Pointer configuration);
    ~QtVideoOutput() override;

    bool open() override;
    bool init() override;
    void write(uint64_t timestamp, const aasdk::common::DataConstBuffer& buffer) override;
    void stop() override;
    QWidget* getVideoWidget() const { return videoWidget_.get(); }

signals:
    void startPlayback();
    void stopPlayback();

protected slots:
    void createVideoOutput();
    void onStartPlayback();
    void onStopPlayback();
    void presentLatestFrame();

private:
    void initDecoder();
    void cleanupDecoder();
    void cleanupDecoderUnlocked();
    void processDecodedFrame(AVFrame* frame);

    std::unique_ptr<QVideoWidget> videoWidget_;
    QVideoSink* videoSink_{nullptr};

    const AVCodec* codec_{nullptr};
    AVCodecParserContext* parser_{nullptr};
    AVCodecContext* codecContext_{nullptr};
	struct AVBufferRef* hwDeviceContext_{nullptr};
    AVPacket* packet_{nullptr};
    AVFrame* frame_{nullptr};
    AVFrame* softwareFrame_{nullptr};
    SwsContext* swsContext_{nullptr};
    std::mutex decoderMutex_;

    std::mutex frameMutex_;
    QVideoFrame latestFrame_;
    bool hasPendingFrame_{false};
};

}
}
}
}
