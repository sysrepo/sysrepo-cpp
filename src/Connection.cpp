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
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/utils/exception.hpp>
#include "utils/exception.hpp"

namespace sysrepo {

Connection::Connection()
    : ctx(nullptr)
{
    sr_conn_ctx_t* ctx;
    auto res = sr_connect(0, &ctx);

    throwIfError(res, "Couldn't connect to sysrepo");
    this->ctx = std::shared_ptr<sr_conn_ctx_t>(ctx, sr_disconnect);
}
}
