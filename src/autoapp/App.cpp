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

#include <thread>
#include <f1x/aasdk/USB/AOAPDevice.hpp>
#include <f1x/aasdk/TCP/TCPEndpoint.hpp>
#include <f1x/openauto/autoapp/App.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{

App::App(boost::asio::io_context& ioService, aasdk::usb::USBWrapper& usbWrapper, aasdk::tcp::ITCPWrapper& tcpWrapper, service::IAndroidAutoEntityFactory& androidAutoEntityFactory,
         aasdk::usb::IUSBHub::Pointer usbHub, aasdk::usb::IConnectedAccessoriesEnumerator::Pointer connectedAccessoriesEnumerator)
    : ioService_(ioService)
    , usbWrapper_(usbWrapper)
    , tcpWrapper_(tcpWrapper)
    , strand_(ioService_)
    , androidAutoEntityFactory_(androidAutoEntityFactory)
    , usbHub_(std::move(usbHub))
    , connectedAccessoriesEnumerator_(std::move(connectedAccessoriesEnumerator))
    , isStopped_(false)
    , retryTimer_(ioService)
    , consecutiveStartupFailures_(0)
    , usbResetsWithoutProgress_(0)
    , lastSessionWasUSB_(false)
{

}

void App::waitForUSBDevice()
{
	boost::asio::post(strand_,
		[this, self = this->shared_from_this()](){

        this->waitForDevice();
    });
}

void App::start(aasdk::tcp::ITCPEndpoint::SocketPointer socket)
{
	boost::asio::post(strand_,
		[this, self = this->shared_from_this(),
		 socket = std::move(socket)]() mutable {



        if(androidAutoEntity_ != nullptr)
        {
            tcpWrapper_.close(*socket);
            OPENAUTO_LOG(warning) << "[App] android auto entity is still running.";
            return;
        }

        try
        {
            usbHub_->cancel();
            connectedAccessoriesEnumerator_->cancel();

            auto tcpEndpoint(std::make_shared<aasdk::tcp::TCPEndpoint>(tcpWrapper_, std::move(socket)));
            androidAutoEntity_ = androidAutoEntityFactory_.create(std::move(tcpEndpoint));
            androidAutoEntity_->start(*this);
            lastSessionWasUSB_ = false;
        }
        catch(const aasdk::error::Error& error)
        {
            OPENAUTO_LOG(error) << "[App] TCP AndroidAutoEntity create error: " << error.what();

            androidAutoEntity_.reset();
            this->waitForDevice();
        }
    });
}

void App::stop()
{
	boost::asio::post(strand_,
		[this, self = this->shared_from_this()]() {

        isStopped_ = true;
        connectedAccessoriesEnumerator_->cancel();
        usbHub_->cancel();
        retryTimer_.cancel();

        if(androidAutoEntity_ != nullptr)
        {
            androidAutoEntity_->stop();
            androidAutoEntity_.reset();
        }
    });
}

void App::aoapDeviceHandler(aasdk::usb::DeviceHandle deviceHandle)
{
    OPENAUTO_LOG(info) << "[App] Device connected.";

    if(androidAutoEntity_ != nullptr)
    {
        OPENAUTO_LOG(warning) << "[App] android auto entity is still running.";
        return;
    }

        try
        {
            // The hotplug callback belongs to the discovery phase.  Tear it
            // down before handing the device to the Android Auto session so
            // the next session can register a fresh callback after a
            // disconnect/reconnect cycle.
            connectedAccessoriesEnumerator_->cancel();
            usbHub_->cancel();

            auto aoapDevice(aasdk::usb::AOAPDevice::create(usbWrapper_, ioService_, deviceHandle));
        androidAutoEntity_ = androidAutoEntityFactory_.create(std::move(aoapDevice));
        androidAutoEntity_->start(*this);
        lastSessionWasUSB_ = true;
    }
    catch(const aasdk::error::Error& error)
    {
        OPENAUTO_LOG(error) << "[App] USB AndroidAutoEntity create error: " << error.what();

        androidAutoEntity_.reset();
        this->scheduleWaitForDevice(cRetryDelay);
    }
}

void App::enumerateDevices()
{
    auto promise = aasdk::usb::IConnectedAccessoriesEnumerator::Promise::defer(strand_);
    promise->then(    [this, self = this->shared_from_this()](auto result) {
        OPENAUTO_LOG(info) << "[App] Devices enumeration result: " << result;
    },
        [this, self = this->shared_from_this()](auto e) {
            OPENAUTO_LOG(error) << "[App] Devices enumeration failed: " << e.what();
        });

    connectedAccessoriesEnumerator_->enumerate(std::move(promise));
}

void App::waitForDevice()
{
    OPENAUTO_LOG(info) << "[App] Waiting for device...";

    auto promise = aasdk::usb::IUSBHub::Promise::defer(strand_);
    promise->then(std::bind(&App::aoapDeviceHandler, this->shared_from_this(), std::placeholders::_1),
                  std::bind(&App::onUSBHubError, this->shared_from_this(), std::placeholders::_1));
    usbHub_->start(std::move(promise));

    // The hotplug callback only fires for devices attached after it is
    // registered. A phone that is already plugged in (connected before the
    // app was launched, or left in accessory mode by a previous run that did
    // not shut the session down cleanly) generates no hotplug event, so probe
    // the current device list for an AOAP device and connect to it directly.
    // Otherwise fall back to switching a non-accessory device into
    // accessory mode.
    if(this->tryConnectToAlreadyConnectedDevice())
    {
        return;
    }

    this->enumerateDevices();
}

void App::scheduleWaitForDevice(std::chrono::milliseconds delay)
{
    // Never retry synchronously: the failed attempt may still hold the USB
    // interface (the torn-down entity releases it asynchronously), so an
    // immediate rescan would fail with LIBUSB_ERROR_BUSY and recurse until
    // the stack overflows. A short delay lets the teardown complete and the
    // phone settle before probing again.
    boost::asio::post(strand_,
        [this, self = this->shared_from_this(), delay]() {

        if(isStopped_)
        {
            return;
        }

        retryTimer_.cancel();

        retryTimer_.expires_after(delay);
        retryTimer_.async_wait([this, self = this->shared_from_this()](const boost::system::error_code& error) {
            if(error || isStopped_ || androidAutoEntity_ != nullptr)
            {
                return;
            }
            this->waitForDevice();
        });
    });
}

bool App::resetAOAPDevice()
{
    for(auto productId : {cAOAPId, cAOAPWithAdbId})
    {
        auto handle = usbWrapper_.openDeviceWithVidPid(cGoogleVendorId, productId);
        if(handle == nullptr)
        {
            continue;
        }

        if(usbWrapper_.resetDevice(handle) == 0)
        {
            OPENAUTO_LOG(info) << "[App] USB device reset.";
            return true;
        }

        // A non-zero return is expected when the reset itself causes
        // re-enumeration: the handle goes stale while the phone restarts its
        // accessory stack. The follow-up device scan verifies the outcome.
        OPENAUTO_LOG(warning) << "[App] USB device reset returned an error, verifying via rescan.";
        return true;
    }

    OPENAUTO_LOG(warning) << "[App] AOAP device not found for reset.";
    return false;
}

bool App::isAOAPDevice(const libusb_device_descriptor& deviceDescriptor) const
{
    return deviceDescriptor.idVendor == cGoogleVendorId &&
            (deviceDescriptor.idProduct == cAOAPId || deviceDescriptor.idProduct == cAOAPWithAdbId);
}

bool App::tryConnectToAlreadyConnectedDevice()
{
    if(androidAutoEntity_ != nullptr)
    {
        return true;
    }

    aasdk::usb::DeviceListHandle deviceList;
    if(usbWrapper_.getDeviceList(deviceList) < 0 || deviceList == nullptr || deviceList->empty())
    {
        return false;
    }

    for(auto device : *deviceList)
    {
        libusb_device_descriptor deviceDescriptor;
        if(usbWrapper_.getDeviceDescriptor(device, deviceDescriptor) != 0)
        {
            continue;
        }

        if(!this->isAOAPDevice(deviceDescriptor))
        {
            continue;
        }

        aasdk::usb::DeviceHandle handle;
        if(usbWrapper_.open(device, handle) != 0 || handle == nullptr)
        {
            continue;
        }

        OPENAUTO_LOG(info) << "[App] Found already connected AOAP device.";
        this->aoapDeviceHandler(std::move(handle));
        return true;
    }

    return false;
}

void App::onAndroidAutoQuit()
{
	boost::asio::post(strand_,[this, self = this->shared_from_this()]() {

        OPENAUTO_LOG(info) << "[App] quit.";

        if(androidAutoEntity_ == nullptr)
        {
            return;
        }

        const bool wasUSBSession = lastSessionWasUSB_;
        const bool authCompleted = androidAutoEntity_->isAuthCompleted();
        lastSessionWasUSB_ = false;

        auto entity = std::move(androidAutoEntity_);
        entity->stop();

        if(isStopped_)
        {
            return;
        }

        // A USB session that quits without completing the SSL handshake means
        // the phone is wedged in a stale session: it stays silent or answers
        // with data from the old crypto context that can never decrypt. After
        // a few such failures in a row, reset the USB device (software
        // equivalent of unplug/replug) so the phone restarts its accessory
        // stack, then probe again after re-enumeration. Resets are capped so
        // a fundamentally unresponsive phone does not cause endless USB churn;
        // plain retries continue and recover automatically once the phone
        // cooperates (any completed handshake clears all counters).
        if(wasUSBSession && !authCompleted)
        {
            ++consecutiveStartupFailures_;
        }
        else
        {
            consecutiveStartupFailures_ = 0;
            usbResetsWithoutProgress_ = 0;
        }

        if(consecutiveStartupFailures_ >= cMaxConsecutiveStartupFailures &&
           usbResetsWithoutProgress_ < cMaxUsbResetsWithoutProgress)
        {
            consecutiveStartupFailures_ = 0;
            ++usbResetsWithoutProgress_;
            OPENAUTO_LOG(warning) << "[App] session failed repeatedly during startup, resetting the USB device.";
            this->resetAOAPDevice();
            this->scheduleWaitForDevice(cResetRecoveryDelay);
        }
        else
        {
            this->scheduleWaitForDevice(cRetryDelay);
        }
    });
}

void App::onUSBHubError(const aasdk::error::Error& error)
{
    OPENAUTO_LOG(error) << "[App] usb hub error: " << error.what();

    if(error != aasdk::error::ErrorCode::OPERATION_ABORTED &&
       error != aasdk::error::ErrorCode::OPERATION_IN_PROGRESS)
    {
        this->waitForDevice();
    }
}

}
}
}
