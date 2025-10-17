/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <optional>
#include <chrono>
#include <memory>
#include <string>
#include <sysrepo-cpp/Enum.hpp>

struct sr_conn_ctx_s;

/**
 * @brief The sysrepo-cpp namespace.
 */
namespace sysrepo {
class Connection;
class Session;

struct ModuleReplaySupport {
    bool enabled;
    std::optional<std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>> earliestNotification;
};

Connection wrapUnmanagedConnection(std::shared_ptr<sr_conn_ctx_s> conn);
/**
 * @brief Handles a connection to sysrepo.
 */
class Connection {
public:
    Connection(const ConnectionFlags options = ConnectionFlags::Default);
    Session sessionStart(sysrepo::Datastore datastore = sysrepo::Datastore::Running);

    uint32_t getId() const;

    ModuleReplaySupport getModuleReplaySupport(const std::string& moduleName);
    void setModuleReplaySupport(const std::string& moduleName, bool enabled);

    friend Connection wrapUnmanagedConnection(std::shared_ptr<sr_conn_ctx_s> conn);
    friend Session;

private:
    explicit Connection(std::shared_ptr<sr_conn_ctx_s> ctx);
    std::shared_ptr<sr_conn_ctx_s> ctx;
};
}
