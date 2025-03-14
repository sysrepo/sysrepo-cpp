/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo-cpp/Session.hpp>

namespace sysrepo {
Session wrapUnmanagedSession(sr_session_ctx_s* session);
void setLogLevelStderr(const LogLevel);
std::optional<libyang::DataNodeOpaque> findMatchingDiscard(libyang::DataNode root, const std::string& xpath);
}
