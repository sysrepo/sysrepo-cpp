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
#include "utils/enum.hpp"
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

Connection::Connection(std::shared_ptr<sr_conn_ctx_t> ctx)
    : ctx(ctx)
{
}

Session Connection::sessionStart(sysrepo::Datastore datastore)
{
    sr_session_ctx_t* sess;
    auto res = sr_session_start(ctx.get(), toDatastore(datastore), &sess);

    throwIfError(res, "Couldn't connect to sysrepo");
    return Session{sess, ctx};
}
}
