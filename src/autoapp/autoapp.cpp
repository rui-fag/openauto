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
#include <QApplication>
#include <QFileInfo>
#include <f1x/aasdk/USB/USBHub.hpp>
#include <f1x/aasdk/USB/ConnectedAccessoriesEnumerator.hpp>
#include <f1x/aasdk/USB/AccessoryModeQueryChain.hpp>
#include <f1x/aasdk/USB/AccessoryModeQueryChainFactory.hpp>
#include <f1x/aasdk/USB/AccessoryModeQueryFactory.hpp>
#include <f1x/aasdk/TCP/TCPWrapper.hpp>
#include <f1x/openauto/autoapp/App.hpp>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <f1x/openauto/autoapp/Configuration/RecentAddressesList.hpp>
#include <f1x/openauto/autoapp/Service/AndroidAutoEntityFactory.hpp>
#include <f1x/openauto/autoapp/Service/ServiceFactory.hpp>
#include <f1x/openauto/autoapp/Configuration/Configuration.hpp>
#include <f1x/openauto/autoapp/UI/MainWindow.hpp>
#include <f1x/openauto/autoapp/UI/SettingsWindow.hpp>
#include <f1x/openauto/autoapp/UI/ConnectDialog.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace aasdk = f1x::aasdk;
namespace autoapp = f1x::openauto::autoapp;
using ThreadPool = std::vector<std::thread>;

namespace
{
void configureMediaEnvironment()
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
        (qEnvironmentVariable("XDG_SESSION_TYPE") == QStringLiteral("wayland") ||
         !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")))
    {
        qputenv("QT_QPA_PLATFORM", "wayland");
    }

    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND"))
    {
        qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    }

    if (qEnvironmentVariableIsEmpty("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
    {
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "vaapi");
    }

    if (qEnvironmentVariableIsEmpty("LIBVA_DRIVER_NAME"))
    {
        const QStringList candidateDrivers = {QStringLiteral("iHD"), QStringLiteral("intel"), QStringLiteral("i965")};
        const QStringList candidatePaths = {
            QStringLiteral("/usr/lib/x86_64-linux-gnu/dri/%1_drv_video.so"),
            QStringLiteral("/usr/lib/x86_64-linux-gnu/dri/%1_dri.so"),
            QStringLiteral("/usr/lib/dri/%1_drv_video.so"),
            QStringLiteral("/usr/lib/dri/%1_dri.so")};

        for (const auto& driver : candidateDrivers)
        {
            for (const auto& pathPattern : candidatePaths)
            {
                const auto path = pathPattern.arg(driver);
                if (QFileInfo::exists(path))
                {
                    qputenv("LIBVA_DRIVER_NAME", driver.toLatin1());
                    return;
                }
            }
        }
    }
}
}

void startUSBWorkers(boost::asio::io_context& ioService, libusb_context* usbContext, ThreadPool& threadPool)
{
    auto usbWorker = [&ioService, usbContext]() {
        timeval libusbEventTimeout{180, 0};

        while(!ioService.stopped())
        {
            libusb_handle_events_timeout_completed(usbContext, &libusbEventTimeout, nullptr);
        }
    };

    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
}

void startIOServiceWorkers(boost::asio::io_context& ioService, ThreadPool& threadPool)
{
    auto ioServiceWorker = [&ioService]() {
        ioService.run();
    };

    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
}

int main(int argc, char* argv[])
{
    configureMediaEnvironment();

    libusb_context* usbContext;
    if(libusb_init(&usbContext) != 0)
    {
        OPENAUTO_LOG(error) << "[OpenAuto] libusb init failed.";
        return 1;
    }

    boost::asio::io_context ioService;
	auto executor_work_guard = boost::asio::make_work_guard(ioService);
    std::vector<std::thread> threadPool;
    startUSBWorkers(ioService, usbContext, threadPool);
    startIOServiceWorkers(ioService, threadPool);

    QApplication qApplication(argc, argv);
    autoapp::ui::MainWindow mainWindow;
    mainWindow.setWindowFlags(Qt::WindowStaysOnTopHint);

    auto configuration = std::make_shared<autoapp::configuration::Configuration>();
    autoapp::ui::SettingsWindow settingsWindow(configuration);
    settingsWindow.setWindowFlags(Qt::WindowStaysOnTopHint);

    autoapp::configuration::RecentAddressesList recentAddressesList(7);
    recentAddressesList.read();

    aasdk::tcp::TCPWrapper tcpWrapper;
    autoapp::ui::ConnectDialog connectDialog(ioService, tcpWrapper, recentAddressesList);
    connectDialog.setWindowFlags(Qt::WindowStaysOnTopHint);

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::exit, []() { std::exit(0); });
    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openSettings, &settingsWindow, &autoapp::ui::SettingsWindow::showFullScreen);
    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openConnectDialog, &connectDialog, &autoapp::ui::ConnectDialog::exec);

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::toggleCursor, [&qApplication]() {
        const auto cursor = qApplication.overrideCursor()->shape() == Qt::BlankCursor ? Qt::ArrowCursor : Qt::BlankCursor;
        qApplication.setOverrideCursor(cursor);
    });

    aasdk::usb::USBWrapper usbWrapper(usbContext);
    aasdk::usb::AccessoryModeQueryFactory queryFactory(usbWrapper, ioService);
    aasdk::usb::AccessoryModeQueryChainFactory queryChainFactory(usbWrapper, ioService, queryFactory);
    autoapp::service::ServiceFactory serviceFactory(ioService, configuration);
    autoapp::service::AndroidAutoEntityFactory androidAutoEntityFactory(ioService, configuration, serviceFactory);

    auto usbHub(std::make_shared<aasdk::usb::USBHub>(usbWrapper, ioService, queryChainFactory));
    auto connectedAccessoriesEnumerator(std::make_shared<aasdk::usb::ConnectedAccessoriesEnumerator>(usbWrapper, ioService, queryChainFactory));
    auto app = std::make_shared<autoapp::App>(ioService, usbWrapper, tcpWrapper, androidAutoEntityFactory, std::move(usbHub), std::move(connectedAccessoriesEnumerator));

    QObject::connect(&connectDialog, &autoapp::ui::ConnectDialog::connectionSucceed, [&app](auto socket) {
        app->start(std::move(socket));
    });

    app->waitForUSBDevice();

    auto result = qApplication.exec();
    std::for_each(threadPool.begin(), threadPool.end(), std::bind(&std::thread::join, std::placeholders::_1));

    libusb_exit(usbContext);
    return result;
}
