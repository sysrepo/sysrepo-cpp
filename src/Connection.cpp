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

/**
 * Creates a new connection to sysrepo. The lifetime of it is managed automatically.
 *
 * Wraps `sr_connect`.
 */
Connection::Connection()
    : ctx(nullptr)
{
    sr_conn_ctx_t* ctx;
    auto res = sr_connect(0, &ctx);

    throwIfError(res, "Couldn't connect to sysrepo");
    this->ctx = std::shared_ptr<sr_conn_ctx_t>(ctx, sr_disconnect);
}

/**
 * Wraps `shared_ptr` of an already created connection to sysrepo. The lifetime of the connection is managed only by the
 * deleter of the supplied `shared_ptr`.
 */
Connection::Connection(std::shared_ptr<sr_conn_ctx_t> ctx)
    : ctx(ctx)
{
}

/**
 * Starts a new sysrepo session.
 *
 * Wraps `sr_session_start`,
 * @param datastore The datastore which the session should operate on. Default is sysrepo::Datastore::Running.
 * @return The newly created session.
 */
Session Connection::sessionStart(sysrepo::Datastore datastore)
{
    sr_session_ctx_t* sess;
    auto res = sr_session_start(ctx.get(), toDatastore(datastore), &sess);

    throwIfError(res, "Couldn't connect to sysrepo");
    return Session{sess, ctx};
}

void Connection::discardOperationalChanges(const char* xpath, std::optional<Session> session, std::chrono::milliseconds timeout)
{
    auto res = sr_discard_oper_changes(ctx.get(), session ? session->m_sess.get() : nullptr, xpath, timeout.count());

    throwIfError(res, "Couldn't discard operational changes");
}
}
