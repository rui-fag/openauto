/*
*  This file is part of aasdk library project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  aasdk is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  aasdk is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with aasdk. If not, see <http://www.gnu.org/licenses/>.
*/

#include <f1x/aasdk/Messenger/MessageInStream.hpp>
#include <f1x/aasdk/Error/Error.hpp>

namespace f1x
{
namespace aasdk
{
namespace messenger
{

MessageInStream::MessageInStream(boost::asio::io_context& ioService, transport::ITransport::Pointer transport, ICryptor::Pointer cryptor)
    : strand_(ioService)
    , transport_(std::move(transport))
    , cryptor_(std::move(cryptor))
{

}

void MessageInStream::startReceive(ReceivePromise::Pointer promise)
{
	boost::asio::dispatch(strand_,
		[this, self = this->shared_from_this(), promise = std::move(promise)]() mutable {

        if(promise_ == nullptr)
        {
            promise_ = std::move(promise);

            auto transportPromise = transport::ITransport::ReceivePromise::defer(strand_);
            transportPromise->then(
                [this, self = this->shared_from_this()](common::Data data) mutable {
                    this->receiveFrameHeaderHandler(common::DataConstBuffer(data));
                },
                [this, self = this->shared_from_this()](const error::Error& e) mutable {
                    promise_->reject(e);
                    promise_.reset();
                });

            transport_->receive(FrameHeader::getSizeOf(), std::move(transportPromise));
        }
        else
        {
            promise->reject(error::Error(error::ErrorCode::OPERATION_IN_PROGRESS));
        }
    });
}

void MessageInStream::receiveFrameHeaderHandler(const common::DataConstBuffer& buffer)
{
    FrameHeader frameHeader(buffer);
    auto messageIter = messages_.find(frameHeader.getChannelId());

    if(frameHeader.getType() == FrameType::FIRST || frameHeader.getType() == FrameType::BULK)
    {
        if(messageIter != messages_.end())
        {
            messages_.clear();
            message_.reset();
            promise_->reject(error::Error(error::ErrorCode::MESSENGER_INTERTWINED_CHANNELS));
            promise_.reset();
            return;
        }

        message_ = std::make_shared<Message>(frameHeader.getChannelId(), frameHeader.getEncryptionType(), frameHeader.getMessageType());

        if(frameHeader.getType() == FrameType::FIRST)
        {
            messages_.emplace(frameHeader.getChannelId(), message_);
        }
    }
    else if(messageIter == messages_.end())
    {
        messages_.clear();
        message_.reset();
        promise_->reject(error::Error(error::ErrorCode::MESSENGER_INTERTWINED_CHANNELS));
        promise_.reset();
        return;
    }
    else
    {
        message_ = messageIter->second;
    }

    recentFrameType_ = frameHeader.getType();
    const size_t frameSize = FrameSize::getSizeOf(frameHeader.getType() == FrameType::FIRST ? FrameSizeType::EXTENDED : FrameSizeType::SHORT);

    auto transportPromise = transport::ITransport::ReceivePromise::defer(strand_);
    transportPromise->then(
        [this, self = this->shared_from_this()](common::Data data) mutable {
            this->receiveFrameSizeHandler(common::DataConstBuffer(data));
        },
        [this, self = this->shared_from_this()](const error::Error& e) mutable {
            messages_.clear();
            message_.reset();
            promise_->reject(e);
            promise_.reset();
        });

    transport_->receive(frameSize, std::move(transportPromise));
}

void MessageInStream::receiveFrameSizeHandler(const common::DataConstBuffer& buffer)
{
    auto transportPromise = transport::ITransport::ReceivePromise::defer(strand_);
    transportPromise->then(
        [this, self = this->shared_from_this()](common::Data data) mutable {
            this->receiveFramePayloadHandler(common::DataConstBuffer(data));
        },
        [this, self = this->shared_from_this()](const error::Error& e) mutable {
            message_.reset();
            promise_->reject(e);
            promise_.reset();
        });

    FrameSize frameSize(buffer);
    transport_->receive(frameSize.getSize(), std::move(transportPromise));
}

void MessageInStream::receiveFramePayloadHandler(const common::DataConstBuffer& buffer)
{   
    if(message_->getEncryptionType() == EncryptionType::ENCRYPTED)
    {
        try
        {
            cryptor_->decrypt(message_->getPayload(), buffer);
        }
        catch(const error::Error& e)
        {
            messages_.erase(message_->getChannelId());
            message_.reset();
            promise_->reject(e);
            promise_.reset();
            return;
        }
    }
    else
    {
        message_->insertPayload(buffer);
    }

    if(recentFrameType_ == FrameType::BULK || recentFrameType_ == FrameType::LAST)
    {
        auto channelId = message_->getChannelId();
        auto completedMessage = std::move(message_);
        messages_.erase(channelId);
        promise_->resolve(std::move(completedMessage));
        promise_.reset();
    }
    else
    {
        auto transportPromise = transport::ITransport::ReceivePromise::defer(strand_);
        transportPromise->then(
            [this, self = this->shared_from_this()](common::Data data) mutable {
                this->receiveFrameHeaderHandler(common::DataConstBuffer(data));
            },
            [this, self = this->shared_from_this()](const error::Error& e) mutable {
                messages_.clear();
                message_.reset();
                promise_->reject(e);
                promise_.reset();
            });

        transport_->receive(FrameHeader::getSizeOf(), std::move(transportPromise));
    }
}

}
}
}
