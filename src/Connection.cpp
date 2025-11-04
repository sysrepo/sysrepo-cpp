/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <algorithm>
extern "C" {
#include <sysrepo.h>
}
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/utils/exception.hpp>
#include "utils/enum.hpp"
#include "utils/exception.hpp"
#include "utils/misc.hpp"

namespace sysrepo {

namespace {
/**
 * @brief Convert a list of filepaths to a null-terminated vector of C strings, this
 * might be dangerous if invoked on a temporary @p vec.
 *
 * @param[in] vec Vector of filepaths.
 * @return Converted null-terminated vector of C strings.
 */
std::vector<const char*> unsafeCStringArray(const std::vector<std::filesystem::path>& vec)
{
    std::vector<const char*> res;
    res.reserve(vec.size() + 1 /* trailing nullptr */);
    std::transform(vec.begin(), vec.end(), std::back_inserter(res), [](const auto& x) { return x.c_str(); });
    res.push_back(nullptr);
    return res;
}

/**
 * @brief Convert a list of strings to a null-terminated vector of C strings, this
 * might be dangerous if invoked on a temporary @p vec.
 *
 * @param[in] vec Vector of strings.
 * @return Converted null-terminated vector of C strings.
 */
std::vector<const char*> unsafeCStringArray(const std::vector<std::string>& vec)
{
    std::vector<const char*> res;
    res.reserve(vec.size() + 1 /* trailing nullptr */);
    std::transform(vec.begin(), vec.end(), std::back_inserter(res), [](const auto& x) { return x.c_str(); });
    res.push_back(nullptr);
    return res;
}

/**
 * @brief Convert a list of lists of strings to a null-terminated vector of pointers
 * to C strings, this might be dangerous if invoked on a temporary @p vec.
 *
 * @param[in] vec Vector of vectors of strings.
 * @return Converted null-terminated vector of null-terminated C arrays of C strings.
 */
std::vector<const char**> unsafeCStringArray(const std::vector<std::vector<std::string>>& vec)
{
    std::vector<const char**> res;
    std::vector<const char*> tmp;

    res.reserve(vec.size() + 1 /* trailing nullptr */);
    for (const auto &v : vec) {
        tmp.reserve(v.size() + 1 /* trailing nullptr */);
        std::transform(v.begin(), v.end(), std::back_inserter(tmp), [](const auto& x) { return x.c_str(); });
        tmp.push_back(nullptr);
        res.push_back(tmp.data());
    }
    res.push_back(nullptr);

    return res;
}
}

/**
 * @brief Install all YANG modules specified in the @p schemaPaths list.
 *
 * Wraps `sr_install_modules`
 */
void Connection::installModules(const std::vector<std::filesystem::path>& schemaPaths,
            const std::optional<std::filesystem::path>& searchDirs,
            const std::optional<std::vector<std::vector<std::string>>>& features)
{
    auto schemaPathsC = unsafeCStringArray(schemaPaths);
    auto featuresC = features ? unsafeCStringArray(*features) : std::vector<const char**>();
    auto res = sr_install_modules(ctx.get(), schemaPathsC.data(),
            searchDirs ? searchDirs->c_str() : nullptr, featuresC.data());

    throwIfError(res, "Couldn't install modules");
}

/**
 * @brief Remove all YANG modules specified in the @p modules list.
 *
 * Wraps `sr_remove_modules`
 */
void Connection::removeModules(const std::vector<std::string>& modules)
{
    auto modulesC = unsafeCStringArray(modules);
    auto res = sr_remove_modules(ctx.get(), modulesC.data(), 1);

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
