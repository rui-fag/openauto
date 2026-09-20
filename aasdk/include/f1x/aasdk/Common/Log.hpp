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

#pragma once

#include <boost/log/trivial.hpp>

namespace aasdk::logging
{
class NullLog
{
public:
    template<typename T>
    constexpr const NullLog& operator<<(T&&) const noexcept
    {
        return *this;
    }
};
}

#if defined(AASDK_LOG_ERRORS_ONLY)
#define AASDK_LOG_error BOOST_LOG_TRIVIAL(error) << "[AaSdk] "
#define AASDK_LOG_debug if constexpr (false) ::aasdk::logging::NullLog()
#define AASDK_LOG_info if constexpr (false) ::aasdk::logging::NullLog()
#define AASDK_LOG_warning if constexpr (false) ::aasdk::logging::NullLog()
#define AASDK_LOG(severity) AASDK_LOG_##severity
#else
#define AASDK_LOG(severity) BOOST_LOG_TRIVIAL(severity) << "[AaSdk] "
#endif
