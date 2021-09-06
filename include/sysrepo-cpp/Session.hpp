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
struct sr_val_s;

namespace sysrepo {
class Connection;
class Session;

class Value {
    friend Session;
    Value(sr_val_s*);

    std::unique_ptr<sr_val_s, void(*)(sr_val_s*)> m_val;
};

class Session {
public:
    // TODO: allow all arguments
    void setItem(const char* path, const char* value);
    Value getItem(const char* path, uint32_t timeoutMs = 0);
private:
    friend Connection;
    Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn);

    std::shared_ptr<sr_conn_ctx_s> m_conn;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};
}
