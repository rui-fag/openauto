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

#include <f1x/openauto/autoapp/Service/Pinger.hpp>
#include <f1x/openauto/Common/Log.hpp>
#include <chrono>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace service
{

Pinger::Pinger(boost::asio::io_context& ioService, time_t duration)
    : strand_(ioService)
    , timer_(ioService)
    , duration_(duration)
    , cancelled_(false)
    , pingsCount_(0)
    , pongsCount_(0)
{

}

void Pinger::ping(Promise::Pointer promise)
{
    boost::asio::post(
        strand_,
        [this,
         self = this->shared_from_this(),
         promise = std::move(promise)]() mutable
        {
            cancelled_ = false;

            if (promise_ != nullptr)
            {
                OPENAUTO_LOG(error)
                    << "[Pinger] ping already in progress";

                promise_->reject(
                    aasdk::error::Error(
                        aasdk::error::ErrorCode::OPERATION_IN_PROGRESS));
            }
            else
            {
                ++pingsCount_;

                OPENAUTO_LOG(debug)
                    << "[Pinger] ping started: pings="
                    << pingsCount_
                    << " pongs="
                    << pongsCount_;

                promise_ = std::move(promise);

                timer_.expires_after(
                    std::chrono::milliseconds(duration_));

                timer_.async_wait(
                    boost::asio::bind_executor(
                        strand_,
                        std::bind(
                            &Pinger::onTimerExceeded,
                            self,
                            std::placeholders::_1)));
            }
        });
}

void Pinger::pong()
{
    boost::asio::post(
        strand_,
        [this, self = this->shared_from_this()]() mutable
        {
            ++pongsCount_;

            OPENAUTO_LOG(debug)
                << "[Pinger] pong: pings="
                << pingsCount_
                << " pongs="
                << pongsCount_;
        });
}

void Pinger::onTimerExceeded(
    const boost::system::error_code& error)
{
    OPENAUTO_LOG(debug)
        << "[Pinger] timer: error="
        << error.value()
        << " cancelled="
        << cancelled_
        << " pings="
        << pingsCount_
        << " pongs="
        << pongsCount_;

	if(promise_ == nullptr)
	{
		return;
	}
	else if(error == boost::asio::error::operation_aborted || cancelled_)
	{
		promise_->reject(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_ABORTED));
	}
	else if(pingsCount_ - pongsCount_ > 1)
	{
		promise_->reject(aasdk::error::Error());
	}
	else
	{
		promise_->resolve();
	}

	promise_.reset();
}

void Pinger::cancel()
{
    boost::asio::post(
        strand_,
        [this, self = this->shared_from_this()]() mutable
        {
            cancelled_ = true;
            timer_.cancel();

            OPENAUTO_LOG(debug)
                << "[Pinger] cancel";
        });
}

}
}
}


}
