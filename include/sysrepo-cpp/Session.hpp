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
#include <optional>
#include <libyang-cpp/DataNode.hpp>
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo-cpp/Subscription.hpp>

struct sr_conn_ctx_s;
struct sr_session_ctx_s;
struct sr_val_s;

namespace sysrepo {
class Connection;
class Session;

class Session {
public:
    Datastore activeDatastore() const;
    // TODO: allow all arguments
    void setItem(const char* path, const char* value);
    // TODO: allow all arguments
    std::optional<libyang::DataNode> getData(const char* path);
    void applyChanges(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

    [[nodiscard]] Subscription onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath = nullptr, uint32_t priority = 0, const SubOptions opts = SubOptions::Default);

private:
    friend Connection;
    friend Session wrapUnmanagedSession(sr_session_ctx_s* session);

    Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn);
    Session(sr_session_ctx_s* unmanagedSession);

    std::shared_ptr<sr_conn_ctx_s> m_conn;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};
}
