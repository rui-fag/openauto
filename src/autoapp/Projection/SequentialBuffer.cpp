#include <f1x/openauto/autoapp/Projection/SequentialBuffer.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

SequentialBuffer::SequentialBuffer()
{
}

bool SequentialBuffer::isSequential() const
{
    return true;
}

bool SequentialBuffer::open(OpenMode mode)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return QIODevice::open(mode);
}

qint64 SequentialBuffer::readData(char *data, qint64 maxlen)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (data_.isEmpty())
        return 0;

    const qint64 len = std::min(maxlen, static_cast<qint64>(data_.size()));

    std::memcpy(data, data_.constData(), static_cast<size_t>(len));
    data_.remove(0, static_cast<int>(len));

    return len;
}

qint64 SequentialBuffer::writeData(const char *data, qint64 len)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.append(data, static_cast<int>(len));
    }

    emit readyRead();

    return len;
}

qint64 SequentialBuffer::size() const
{
    return bytesAvailable();
}

qint64 SequentialBuffer::pos() const
{
    return 0;
}

bool SequentialBuffer::seek(qint64)
{
    return false;
}

bool SequentialBuffer::atEnd() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    // This is a live stream. An empty buffer does NOT mean EOF.
    return false;
}

bool SequentialBuffer::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);

    data_.clear();

    return true;
}

qint64 SequentialBuffer::bytesAvailable() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return QIODevice::bytesAvailable() +
           static_cast<qint64>(data_.size());
}

bool SequentialBuffer::canReadLine() const
{
    return false;
}

}
}
}
}
