/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <numeric>

extern "C" {
#include <sysrepo.h>
}
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/utils/exception.hpp>
#include "utils/enum.hpp"
#include "utils/exception.hpp"
#include "utils/misc.hpp"

namespace sysrepo {

/**
 * Creates a new connection to sysrepo. The lifetime of it is managed automatically.
 *
 * Wraps `sr_connect`.
 */
Connection::Connection(const ConnectionFlags options)
    : ctx(nullptr)
{
    sr_conn_ctx_t* ctx;
    auto res = sr_connect(static_cast<sr_conn_flag_t>(options), &ctx);

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
 * Wraps `sr_session_start`.
 * @param datastore The datastore which the session should operate on. Default is sysrepo::Datastore::Running.
 * @return The newly created session.
 */
Session Connection::sessionStart(sysrepo::Datastore datastore)
{
    sr_session_ctx_t* sess;
    auto res = sr_session_start(ctx.get(), toDatastore(datastore), &sess);

    throwIfError(res, "Couldn't start sysrepo session");
    return Session{sess, *this};
}

/**
 * Change module replay support
 *
 * Wraps `sr_set_module_replay_support`.
 */
void Connection::setModuleReplaySupport(const std::string& moduleName, bool enabled)
{
    auto res = sr_set_module_replay_support(ctx.get(), moduleName.c_str(), enabled);
    throwIfError(res, "Couldn't set replay support for module '" + moduleName + "'");
}

/**
 * Returns information about replay support of a module
 *
 * Wraps `sr_get_module_replay_support`.
 */
ModuleReplaySupport Connection::getModuleReplaySupport(const std::string& moduleName)
{
    int enabled;
    struct timespec earliestNotif;
    auto res = sr_get_module_replay_support(ctx.get(), moduleName.c_str(), &earliestNotif, &enabled);

    throwIfError(res, "Couldn't get replay support for module '" + moduleName + "'");

    if (earliestNotif.tv_sec == 0 && earliestNotif.tv_nsec == 0) {
        return {static_cast<bool>(enabled), std::nullopt};
    }
    return {static_cast<bool>(enabled), toTimePoint(earliestNotif)};
}

/**
 * @brief Get the internal, sysrepo-level connection ID
 *
 * Wraps `sr_get_cid`.
 */
uint32_t Connection::getId() const
{
    return sr_get_cid(ctx.get());
}

/**
 * Installs a YANG schema.
 *
 * Wraps `sr_install_module`,
 * @param schemaPath path to the schema which should be installed
 * @param searchDirs directory to search for dependent YANG schemas
 * @param features feature which should be enabled
 */
void Connection::installModule(const std::filesystem::path& schemaPath, const std::vector<std::filesystem::path>& searchDirs, const std::vector<std::string>& features)
{
    // Concatenate searchDirs into a colon-separated string -> sysrepo API format
    std::string searchDirsStr = std::accumulate(
        searchDirs.begin(), searchDirs.end(), std::string(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
            return a.empty() ? b.string() : a.string() + ":" + b.string();
        });

    std::vector<const char*> c_strs;
    std::transform(
        std::begin(features), std::end(features), std::back_inserter(c_strs), std::mem_fn(&std::string::c_str));
    c_strs.push_back(nullptr); // Add null terminator
    auto res = sr_install_module(ctx.get(), schemaPath.c_str(), searchDirsStr.c_str(), c_strs.data());
    throwIfError(res, "Installing module '" + schemaPath.string() + "' failed");
}

/**
 * Removes an installed module.
 *
 * Wraps `sr_remove_module`,
 * @param moduleName name of the module to remove
 * @param force remove other installed modules as well that depend on @p moduleName
 */
void Connection::removeModule(const std::string& moduleName, bool force)
{
    auto res = sr_remove_module(ctx.get(), moduleName.c_str(), force);
    throwIfError(res, "Removing module '" + moduleName + "' failed");
}

}
