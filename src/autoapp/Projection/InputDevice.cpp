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

#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Projection/IInputDeviceEventHandler.hpp>
#include <f1x/openauto/autoapp/Projection/InputDevice.hpp>

#include <QApplication>
#include <QMetaObject>
#include <QThread>
#include <QWidget>
#include <QWindow>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

InputDevice::InputDevice(QObject& parent, configuration::IConfiguration::Pointer configuration, const QRect& touchscreenGeometry, const QRect& displayGeometry)
    : parent_(parent)
    , configuration_(std::move(configuration))
    , touchscreenGeometry_(touchscreenGeometry)
    , displayGeometry_(displayGeometry)
    , eventHandler_(nullptr)
{
    this->moveToThread(parent.thread());
}

void InputDevice::start(IInputDeviceEventHandler& eventHandler)
{
    OPENAUTO_LOG(info) << "[InputDevice] start.";
    {
        std::lock_guard<decltype(mutex_)> lock(mutex_);
        eventHandler_ = &eventHandler;
    }

    auto installFilter = [this]() {
        parent_.installEventFilter(this);
        // Also install on every top-level widget (including the fullscreen QVideoWidget
        // which is a separate native window and may not propagate events through QApplication)
        for(QWidget* w : QApplication::topLevelWidgets())
        {
            w->installEventFilter(this);
            // Also install on all children to catch events on internal viewports
            for(QObject* child : w->findChildren<QWidget*>())
            {
                child->installEventFilter(this);
            }
        }
    };
    if(QThread::currentThread() == parent_.thread())
    {
        installFilter();
    }
    else
    {
        QMetaObject::invokeMethod(&parent_, installFilter, Qt::BlockingQueuedConnection);
    }
    OPENAUTO_LOG(debug) << "[InputDevice] application event filter installed; touchscreen enabled="
                         << configuration_->getTouchscreenEnabled();
}

void InputDevice::stop()
{
    OPENAUTO_LOG(info) << "[InputDevice] stop.";

    // Disable callbacks before synchronously touching Qt-owned objects.  The
    // Qt event thread may currently be in eventFilter() and waiting for this
    // mutex; holding it across BlockingQueuedConnection would deadlock the
    // disconnect/reconnect path.
    {
        std::lock_guard<decltype(mutex_)> lock(mutex_);
        eventHandler_ = nullptr;
    }

    auto removeFilter = [this]() {
        parent_.removeEventFilter(this);
        for(QWidget* w : QApplication::topLevelWidgets())
        {
            w->removeEventFilter(this);
            for(QObject* child : w->findChildren<QWidget*>())
            {
                child->removeEventFilter(this);
            }
        }
    };
    if(QThread::currentThread() == parent_.thread())
    {
        removeFilter();
    }
    else
    {
        QMetaObject::invokeMethod(&parent_, removeFilter, Qt::BlockingQueuedConnection);
    }
}

bool InputDevice::eventFilter(QObject* obj, QEvent* event)
{
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    // Dynamically install filter on new top-level widgets (e.g. QVideoWidget shown after start())
    if(event->type() == QEvent::Show || event->type() == QEvent::WinIdChange)
    {
        QWidget* w = qobject_cast<QWidget*>(obj);
        if(w && w->isWindow())
        {
            w->installEventFilter(this);
            for(QObject* child : w->findChildren<QWidget*>())
            {
                child->installEventFilter(this);
            }
            OPENAUTO_LOG(debug) << "[InputDevice] installed filter on new top-level widget: "
                                << obj->metaObject()->className();
        }
    }

    if(eventHandler_ != nullptr)
    {
        if(event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        {
            QKeyEvent* key = static_cast<QKeyEvent*>(event);
            if(!key->isAutoRepeat())
            {
                return this->handleKeyEvent(event, key);
            }
        }
        else if(event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseMove)
        {
            return this->handleTouchEvent(obj, event);
        }
    }

    return QObject::eventFilter(obj, event);
}

bool InputDevice::handleKeyEvent(QEvent* event, QKeyEvent* key)
{
    auto eventType = event->type() == QEvent::KeyPress ? ButtonEventType::PRESS : ButtonEventType::RELEASE;
    aasdk::proto::enums::ButtonCode::Enum buttonCode;
    WheelDirection wheelDirection = WheelDirection::NONE;

    switch(key->key())
    {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        buttonCode = aasdk::proto::enums::ButtonCode::ENTER;
        break;

    case Qt::Key_Left:
        buttonCode = aasdk::proto::enums::ButtonCode::LEFT;
        break;

    case Qt::Key_Right:
        buttonCode = aasdk::proto::enums::ButtonCode::RIGHT;
        break;

    case Qt::Key_Up:
        buttonCode = aasdk::proto::enums::ButtonCode::UP;
        break;

    case Qt::Key_Down:
        buttonCode = aasdk::proto::enums::ButtonCode::DOWN;
        break;

    case Qt::Key_Escape:
        buttonCode = aasdk::proto::enums::ButtonCode::BACK;
        break;

    case Qt::Key_H:
        buttonCode = aasdk::proto::enums::ButtonCode::HOME;
        break;

    case Qt::Key_P:
        buttonCode = aasdk::proto::enums::ButtonCode::PHONE;
        break;

    case Qt::Key_O:
        buttonCode = aasdk::proto::enums::ButtonCode::CALL_END;
        break;

    case Qt::Key_X:
        buttonCode = aasdk::proto::enums::ButtonCode::PLAY;
        break;

    case Qt::Key_C:
        buttonCode = aasdk::proto::enums::ButtonCode::PAUSE;
        break;

    case Qt::Key_MediaPrevious:
    case Qt::Key_V:
        buttonCode = aasdk::proto::enums::ButtonCode::PREV;
        break;

    case Qt::Key_MediaPlay:
    case Qt::Key_B:
        buttonCode = aasdk::proto::enums::ButtonCode::TOGGLE_PLAY;
        break;

    case Qt::Key_MediaNext:
    case Qt::Key_N:
        buttonCode = aasdk::proto::enums::ButtonCode::NEXT;
        break;

    case Qt::Key_M:
        buttonCode = aasdk::proto::enums::ButtonCode::MICROPHONE_1;
        break;

    case Qt::Key_1:
        wheelDirection = WheelDirection::LEFT;
        eventType = ButtonEventType::NONE;
        buttonCode = aasdk::proto::enums::ButtonCode::SCROLL_WHEEL;
        break;

    case Qt::Key_2:
        wheelDirection = WheelDirection::RIGHT;
        eventType = ButtonEventType::NONE;
        buttonCode = aasdk::proto::enums::ButtonCode::SCROLL_WHEEL;
        break;

    default:
        return true;
    }

    const auto& buttonCodes = this->getSupportedButtonCodes();
    if(std::find(buttonCodes.begin(), buttonCodes.end(), buttonCode) != buttonCodes.end())
    {
        if(buttonCode != aasdk::proto::enums::ButtonCode::SCROLL_WHEEL || event->type() == QEvent::KeyRelease)
        {
            eventHandler_->onButtonEvent({eventType, wheelDirection, buttonCode});
        }
    }

    return true;
}

bool InputDevice::handleTouchEvent(QObject* obj, QEvent* event)
{
    if(!configuration_->getTouchscreenEnabled())
    {
        return true;
    }

    aasdk::proto::enums::TouchAction::Enum type;

    switch(event->type())
    {
    case QEvent::MouseButtonPress:
        type = aasdk::proto::enums::TouchAction::PRESS;
        break;
    case QEvent::MouseButtonRelease:
        type = aasdk::proto::enums::TouchAction::RELEASE;
        break;
    case QEvent::MouseMove:
        type = aasdk::proto::enums::TouchAction::DRAG;
        break;
    default:
        return true;
    };

	QMouseEvent* mouse = static_cast<QMouseEvent*>(event);

	if(event->type() != QEvent::MouseButtonRelease &&
	   !mouse->buttons().testFlag(Qt::LeftButton))
	{
		return true;
	}

    if(event->type() == QEvent::MouseButtonRelease || mouse->buttons().testFlag(Qt::LeftButton))
    {
		// The event filter is installed on the video widget and its children.
		// QMouseEvent::pos() is relative to the receiving child, so using it
		// directly makes the same physical click map to different AA locations.
		// Always map from the global screen position instead.
		const auto globalPosition = mouse->globalPosition().toPoint();
		const auto screenX = globalPosition.x() - touchscreenGeometry_.left();
		const auto screenY = globalPosition.y() - touchscreenGeometry_.top();
		const auto normalizedX = std::clamp(static_cast<float>(screenX) / touchscreenGeometry_.width(), 0.0f, 1.0f);
		const auto normalizedY = std::clamp(static_cast<float>(screenY) / touchscreenGeometry_.height(), 0.0f, 1.0f);
		const uint32_t x = static_cast<uint32_t>(normalizedX * displayGeometry_.width());
		const uint32_t y = static_cast<uint32_t>(normalizedY * displayGeometry_.height());
		QWidget* widget = qobject_cast<QWidget*>(obj);
		const auto windowGeometry = widget == nullptr
			? QRect{}
			: QRect(widget->window()->mapToGlobal(QPoint(0, 0)), widget->window()->size());
		OPENAUTO_LOG(debug) << "[InputDevice]"
			<< "x: " << x
			<< ", y: " << y
			<< ", type: " << type
			<< ", object: " << obj->metaObject()->className()
			<< ", global: (" << globalPosition.x() << "," << globalPosition.y() << ")"
			<< ", screen: " << touchscreenGeometry_.x() << "," << touchscreenGeometry_.y()
			<< " " << touchscreenGeometry_.width() << "x" << touchscreenGeometry_.height()
			<< ", video: " << displayGeometry_.width() << "x" << displayGeometry_.height()
			<< ", window: " << windowGeometry.x() << "," << windowGeometry.y()
			<< " " << windowGeometry.width() << "x" << windowGeometry.height();
        eventHandler_->onTouchEvent({type, x, y, 0});
	}
		return true;
}

bool InputDevice::hasTouchscreen() const
{
    return configuration_->getTouchscreenEnabled();
}

QRect InputDevice::getTouchscreenGeometry() const
{
    return touchscreenGeometry_;
}

QRect InputDevice::getDisplayGeometry() const
{
	return displayGeometry_;
}

InputDevice::ButtonCodes InputDevice::getSupportedButtonCodes() const
{
    return configuration_->getButtonCodes();
}

}
}
}
}
