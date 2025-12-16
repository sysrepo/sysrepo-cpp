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
}

/**
 * @brief Install all YANG modules specified in the @p schemaPaths list.
 *
 * Wraps `sr_install_modules2`
 */
void Connection::installModules(const std::vector<struct ModuleInstallation>& modules,
                                const std::optional<std::filesystem::path>& searchDirs,
                                const std::variant<std::monostate, std::filesystem::path, std::string>& initialData,
                                const libyang::DataFormat dataFormat)
{
    std::vector<const char*> features;
    std::vector<std::vector<const char*>> allFeatures;
    std::string schema;
    std::vector<std::string> allSchemas;
    std::vector<sr_install_mod_t> mods;
    const std::filesystem::path* initPath = std::get_if<std::filesystem::path>(&initialData);
    const std::string* initData = std::get_if<std::string>(&initialData);

    /* prepare memory so that passed pointers to sr_install_modules2() are valid */
    for (auto& module : modules) {
        /* save so that .data() call is valid */
        allFeatures.push_back(unsafeCStringArray(module.features));

        if (holds_alternative<std::filesystem::path>(module.schema)) {
            /* save so that .c_str() call is valid */
            allSchemas.push_back(std::get<std::filesystem::path>(module.schema).string());
        } else {
            allSchemas.push_back(std::get<std::string>(module.schema));
        }
    }
    for (std::size_t i = 0; i < modules.size(); ++i) {
        mods.push_back(sr_install_mod_t{.schema_path = allSchemas[i].c_str(),
                                        .features = allFeatures[i].data(),
                                        .module_ds = {nullptr},
                                        .owner = modules[i].owner ? modules[i].owner->c_str() : nullptr,
                                        .group = modules[i].group ? modules[i].group->c_str() : nullptr,
                                        .perm = modules[i].permissions});
    }
    auto res = sr_install_modules2(ctx.get(), mods.data(), mods.size(), searchDirs ? searchDirs->c_str() : nullptr,
            initData ? initData->c_str() : nullptr, initPath ? initPath->c_str() : nullptr, static_cast<LYD_FORMAT>(dataFormat));

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
