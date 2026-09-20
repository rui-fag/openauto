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

#include <QApplication>
#include <f1x/openauto/autoapp/Projection/QtAudioOutput.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

QtAudioOutput::QtAudioOutput(uint32_t channelCount, uint32_t sampleSize, uint32_t sampleRate)
    : audioDevice_(nullptr)
    , playbackStarted_(false)
    , maxQueuedBytes_(static_cast<qint64>(sampleRate) * channelCount * (sampleSize / 8) / 2)
    , sampleSize_(sampleSize)
{
    audioFormat_.setChannelCount(channelCount);
    audioFormat_.setSampleRate(sampleRate);
    audioFormat_.setChannelConfig(QAudioFormat::defaultChannelConfigForChannelCount(channelCount));
    if(sampleSize == 8)
    {
        audioFormat_.setSampleFormat(QAudioFormat::SampleFormat::UInt8);
    }
    else if(sampleSize == 32)
    {
        audioFormat_.setSampleFormat(QAudioFormat::SampleFormat::Int32);
    }
    else
    {
        audioFormat_.setSampleFormat(QAudioFormat::SampleFormat::Int16);
    }

    this->moveToThread(QApplication::instance()->thread());
    connect(this, &QtAudioOutput::startPlayback, this, &QtAudioOutput::onStartPlayback);
    connect(this, &QtAudioOutput::suspendPlayback, this, &QtAudioOutput::onSuspendPlayback);
    connect(this, &QtAudioOutput::stopPlayback, this, &QtAudioOutput::onStopPlayback);
    QMetaObject::invokeMethod(this, "createAudioOutput", Qt::BlockingQueuedConnection);
}

void QtAudioOutput::createAudioOutput()
{
    OPENAUTO_LOG(debug) << "[QtAudioOutput] create.";
    audioOutput_ = std::make_unique<QAudioSink>(QMediaDevices::defaultAudioOutput(), audioFormat_);
    const auto bytesPerFrame = audioFormat_.channelCount() * audioFormat_.bytesPerSample();
    audioOutput_->setBufferSize(audioFormat_.sampleRate() * bytesPerFrame / 10);
    audioPumpTimer_ = std::make_unique<QTimer>(this);
    audioPumpTimer_->setInterval(5);
    connect(audioPumpTimer_.get(), &QTimer::timeout, this, &QtAudioOutput::pumpAudio);
    connect(audioOutput_.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        OPENAUTO_LOG(info) << "[QtAudioOutput] state=" << static_cast<int>(state)
                           << ", error=" << static_cast<int>(audioOutput_->error())
                           << ", bufferSize=" << audioOutput_->bufferSize()
                           << ", bytesFree=" << audioOutput_->bytesFree();
    });
}

bool QtAudioOutput::open()
{
    return true;
}

void QtAudioOutput::write(aasdk::messenger::Timestamp::ValueType, const aasdk::common::DataConstBuffer& buffer)
{
    if(buffer.size == 0)
        return;

    QByteArray audio(reinterpret_cast<const char*>(buffer.cdata), static_cast<int>(buffer.size));
    QMetaObject::invokeMethod(this, [this, audio = std::move(audio)]() mutable {
        pendingAudio_.append(audio);
        const auto queuedBytes = pendingAudio_.size() - pendingAudioOffset_;
        if(queuedBytes > maxQueuedBytes_)
        {
            pendingAudioOffset_ += queuedBytes - maxQueuedBytes_;
        }
        pumpAudio();
    }, Qt::QueuedConnection);
}

void QtAudioOutput::start()
{
    emit startPlayback();
}

void QtAudioOutput::stop()
{
    emit stopPlayback();
}

void QtAudioOutput::suspend()
{
    emit suspendPlayback();
}

uint32_t QtAudioOutput::getSampleSize() const
{
    return sampleSize_;
}

uint32_t QtAudioOutput::getChannelCount() const
{
    return audioFormat_.channelCount();
}

uint32_t QtAudioOutput::getSampleRate() const
{
    return audioFormat_.sampleRate();
}

void QtAudioOutput::onStartPlayback()
{
    if(!playbackStarted_)
    {
        audioDevice_ = audioOutput_->start();
        playbackStarted_ = true;
        audioPumpTimer_->start();
    }
    else
    {
        audioOutput_->resume();
    }
}

void QtAudioOutput::onSuspendPlayback()
{
    audioOutput_->suspend();
}

void QtAudioOutput::onStopPlayback()
{
    if(playbackStarted_)
    {
        audioPumpTimer_->stop();
        audioOutput_->stop();
        audioDevice_ = nullptr;
        pendingAudio_.clear();
        pendingAudioOffset_ = 0;
        playbackStarted_ = false;
    }
}

void QtAudioOutput::pumpAudio()
{
    if(!playbackStarted_ || audioDevice_ == nullptr ||
       pendingAudioOffset_ >= pendingAudio_.size())
        return;

    const auto available = pendingAudio_.size() - pendingAudioOffset_;
    const auto written = audioDevice_->write(
        pendingAudio_.constData() + pendingAudioOffset_, available);
    if(written > 0)
    {
        pendingAudioOffset_ += written;
        if(pendingAudioOffset_ == pendingAudio_.size())
        {
            pendingAudio_.clear();
            pendingAudioOffset_ = 0;
        }
        else if(pendingAudioOffset_ >= 64 * 1024 &&
                pendingAudioOffset_ > pendingAudio_.size() / 2)
        {
            pendingAudio_.remove(0, pendingAudioOffset_);
            pendingAudioOffset_ = 0;
        }
    }
}

}
}
}
}
