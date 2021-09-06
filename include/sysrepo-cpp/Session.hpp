/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <chrono>
#include <memory>
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo-cpp/Value.hpp>

struct sr_conn_ctx_s;
struct sr_session_ctx_s;
struct sr_val_s;

namespace sysrepo {
class Connection;

class Session {
public:
    Datastore activeDatastore() const;
    // TODO: allow all arguments
    void setItem(const char* path, const char* value);
    Value getItem(const char* path, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    void applyChanges(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
private:
    friend Connection;
    Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn);

    std::shared_ptr<sr_conn_ctx_s> m_conn;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};
}
