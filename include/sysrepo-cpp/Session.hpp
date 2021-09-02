/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <memory>
#include <sysrepo-cpp/Enum.hpp>

struct sr_conn_ctx_s;
struct sr_session_ctx_s;

namespace sysrepo {
class Connection;

class Session {
public:
    Datastore activeDatastore() const;
private:
    friend Connection;
    Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn);

    std::shared_ptr<sr_conn_ctx_s> m_conn;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};
}
