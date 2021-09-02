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

namespace sysrepo {

Connection::Connection()
    : ctx(nullptr)
{
    sr_conn_ctx_t* ctx;
    auto res = sr_connect(0, &ctx);
    if (res != SR_ERR_OK) {
        throw ErrorWithCode("Couldn't connect to sysrepo", res);
    }
    this->ctx = ctx;
}

Connection::~Connection()
{
    sr_disconnect(ctx);
}
}
