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
#include "utils/misc.hpp"

namespace sysrepo {

/**
 * @brief Convert a list of strings to a null-terminated list of C strings.
 * 
 * @param[in] list List of strings.
 * @return Converted null-terminated list.
 */
static std::vector<const char*> to_vec_c_str(const std::vector<std::string>& list)
{
    std::vector<const char*> native_list;
    native_list.reserve(1 + list.size());

    // iterate with reference to elements to ensure `elem.c_str()` lives as long as `list`
    // and avoid copying
    for (const auto &elem : list) {
        native_list.push_back(elem.c_str());
    }

    // sysrepo expects this array to be null terminated!
    native_list.push_back(nullptr);

    return native_list;
}

/**
 * @brief Install all YANG modules specified in the @p schema_paths list.
 * 
 * Wraps `sr_install_modules`
 */
void Connection::installModules(const std::vector<std::string>& schema_paths, const std::optional<std::string>& search_dir)
{
    auto schema_paths_c = to_vec_c_str(schema_paths);
    auto res = sr_install_modules(ctx.get(), schema_paths_c.data(),
            search_dir ? search_dir->c_str() : nullptr, nullptr);

    throwIfError(res, "Couldn't install modules");
}

/**
 * @brief Remove all YANG modules specified in the @p modules list.
 * 
 * Wraps `sr_remove_modules`
 */
void Connection::removeModules(const std::vector<std::string>& modules)
{
    auto modules_c = to_vec_c_str(modules);
    auto res = sr_remove_modules(ctx.get(), modules_c.data(), 1);

    throwIfError(res, "Couldn't remove modules");
}

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
}
