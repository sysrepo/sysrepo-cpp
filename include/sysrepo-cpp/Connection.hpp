/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <memory>
#include <sysrepo-cpp/Session.hpp>

struct sr_conn_ctx_s;

namespace sysrepo {
class Connection;

Connection wrapUnmanagedConnection(std::shared_ptr<sr_conn_ctx_s> conn);
class Connection {
public:
    Connection();
    Session sessionStart(sysrepo::Datastore datastore = sysrepo::Datastore::Running);

    friend Connection wrapUnmanagedConnection(std::shared_ptr<sr_conn_ctx_s> conn);

private:
    Connection(std::shared_ptr<sr_conn_ctx_s> ctx);
    std::shared_ptr<sr_conn_ctx_s> ctx;
};
}
