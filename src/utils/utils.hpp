/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once
#include <sysrepo-cpp/Session.hpp>
struct sr_session_ctx_s;

namespace sysrepo {
Session wrapUnmanagedSession(sr_session_ctx_s* session);
}
