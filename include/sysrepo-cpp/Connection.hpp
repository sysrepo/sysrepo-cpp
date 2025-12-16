/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <chrono>
#include <filesystem>
#include <libyang-cpp/Enum.hpp>
#include <memory>
#include <optional>
#include <string>
#include <sysrepo-cpp/Enum.hpp>
#include <variant>
#include <vector>

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

struct ModuleInstallation {
    std::variant<std::filesystem::path, std::string> schema;
    std::vector<std::string> features = {};
    std::optional<std::string> owner = std::nullopt;
    std::optional<std::string> group = std::nullopt;
    mode_t permissions = 0;
    // FIXME: maybe add support for module plugins later
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
    void installModules(const std::vector<ModuleInstallation>& modules,
                        const std::optional<std::filesystem::path>& searchDirs = std::nullopt,
                        const std::variant<std::monostate, std::filesystem::path, std::string>& initialData = {},
                        const libyang::DataFormat dataFormat = libyang::DataFormat::Detect);
    void removeModules(const std::vector<std::string>& modules);

    friend Connection wrapUnmanagedConnection(std::shared_ptr<sr_conn_ctx_s> conn);
    friend Session;

private:
    explicit Connection(std::shared_ptr<sr_conn_ctx_s> ctx);
    std::shared_ptr<sr_conn_ctx_s> ctx;
};
}
