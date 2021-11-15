/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

extern "C" {
#include <sysrepo.h>
}
#include "enum.hpp"
#include "utils.hpp"

namespace sysrepo {
Session wrapUnmanagedSession(sr_session_ctx_s* session)
{
    return Session{session, unmanaged_tag{}};
}

Connection wrapUnmanagedConnection(std::shared_ptr<sr_conn_ctx_s> conn)
{
    return Connection{conn};
}

void setLogLevelStderr(const LogLevel level)
{
    sr_log_stderr(toLogLevel(level));
}

std::timespec toTimespec(std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> tp)
{
    // https://embeddedartistry.com/blog/2019/01/31/converting-between-timespec-stdchrono#-std-chrono-time_point-to-timespec-
    using namespace std::chrono;
    auto secs = time_point_cast<seconds>(tp);
    auto ns = time_point_cast<nanoseconds>(tp) - time_point_cast<nanoseconds>(secs);

    return std::timespec{secs.time_since_epoch().count(), ns.count()};
}

std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> toTimePoint(std::timespec ts)
{
    // https://embeddedartistry.com/blog/2019/01/31/converting-between-timespec-stdchrono#-timespec-to-std-chrono-timepoint-
    using namespace std::chrono;
    auto duration = seconds{ts.tv_sec} + nanoseconds{ts.tv_nsec};
    return time_point<system_clock, nanoseconds>{duration};
}
}
