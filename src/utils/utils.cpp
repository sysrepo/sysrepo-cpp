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

void setLogLevelStderr(const LogLevel level)
{
    sr_log_stderr(toLogLevel(level));
}
}
