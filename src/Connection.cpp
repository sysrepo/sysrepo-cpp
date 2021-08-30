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

namespace sysrepo {

Connection::Connection()
    : ctx(nullptr)
{
    sr_conn_ctx_t* ctx;
    int res = sr_connect(0, &ctx);
    if (res) {
        throw 666; // FIXME
    }
    this->ctx = ctx;
}

Connection::~Connection()
{
    sr_disconnect(ctx);
}

}
