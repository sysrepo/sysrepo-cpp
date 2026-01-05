/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/utils/exception.hpp>
#include <sysrepo-cpp/utils/utils.hpp>
extern "C" {
#include <sysrepo.h>
}
#include "enum.hpp"
#include "exception.hpp"
#include "misc.hpp"

namespace sysrepo {
/**
 * Wraps a session pointer without managing it. Use at your own risk.
 */
Session wrapUnmanagedSession(sr_session_ctx_s* session)
{
    return Session{session, unmanaged_tag{}};
}

Connection wrapUnmanagedConnection(std::shared_ptr<sr_conn_ctx_s> conn)
{
    return Connection{conn};
}

/**
 * Sets the global loglevel for sysrepo.
 */
void setLogLevelStderr(const LogLevel level)
{
    sr_log_stderr(toLogLevel(level));
}

/**
 * @brief Set global sysrepo-level context options
 *
 * Be advised of consequences of manipulating a shared global state, especially when using multiple connections.
 *
 * Wraps `sr_context_options`.
 */
ContextFlags setGlobalContextOptions(const ContextFlags flags, const GlobalContextEffect when)
{
    uint32_t old;
    throwIfError(
        sr_context_options(static_cast<uint32_t>(flags), when == GlobalContextEffect::Immediate, &old),
        "sr_context_options failed",
        nullptr);
    return static_cast<ContextFlags>(old);
}

std::timespec toTimespec(std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> tp)
{
    // https://embeddedartistry.com/blog/2019/01/31/converting-between-timespec-stdchrono#-std-chrono-time_point-to-timespec-
    using namespace std::chrono;
    auto secs = time_point_cast<seconds>(tp);
    auto ns = time_point_cast<nanoseconds>(tp) - time_point_cast<nanoseconds>(secs);

    return std::timespec{secs.time_since_epoch().count(), ns.count()};
}

std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> toTimePoint(std::timespec ts)
{
    // https://embeddedartistry.com/blog/2019/01/31/converting-between-timespec-stdchrono#-timespec-to-std-chrono-timepoint-
    using namespace std::chrono;
    auto duration = seconds{ts.tv_sec} + nanoseconds{ts.tv_nsec};
    return time_point<system_clock, nanoseconds>{duration};
}

/**
 * Checks whether opts include the the NoThread flag.
 * Throws if:
 * - callbacks are available, but NoThread flag is not present.
 * - NoThread flag is present, but callbacks are not available.
 */
void checkNoThreadFlag(const SubscribeOptions opts, const std::optional<FDHandling>& callbacks)
{
    auto includesFlag = opts & SubscribeOptions::NoThread;
    if (callbacks && !includesFlag) {
        throw Error("Setting custom event loop callbacks requires the SubscribeOptions::NoThread flag");
    }

    if (includesFlag && !callbacks) {
        throw Error("CustomEventLoopCallbacks must be present when using SubscribeOptions::NoThread");
    }
}

/**
 * @short If there's a sysrepo:discard-items node which matches the given XPath, return it
 *
 * @see Session::operationalChanges()
 * @see Session::dropForeignOperationalContent()
 */
std::optional<libyang::DataNodeOpaque> findMatchingDiscard(libyang::DataNode root, const std::string& xpath)
{
    auto discard = root.firstOpaqueSibling();
    while (discard) {
        if (discard->name().matches("sysrepo", "discard-items") && discard->value() == xpath) {
            return discard;
        }
        if (auto next = discard->nextSibling()) {
            discard = next->asOpaque();
        } else {
            break;
        }
    }
    return std::nullopt;
}

/**
 * @short Find all sysrepo:discard-items nodes which match the given XPath or the descendants of this XPath
 */
std::vector<libyang::DataNodeOpaque> findMatchingDiscardPrefixes(libyang::DataNode root, const std::string& xpathPrefix)
{
    auto withSlash = (xpathPrefix.empty() || xpathPrefix[xpathPrefix.size() - 1] == '/') ? xpathPrefix : xpathPrefix + '/';
    auto withBracket = (xpathPrefix.empty() || xpathPrefix[xpathPrefix.size() - 1] == '[') ? xpathPrefix : xpathPrefix + '[';
    std::vector<libyang::DataNodeOpaque> res;
    auto discard = root.firstOpaqueSibling();
    while (discard) {
        if (discard->name().matches("sysrepo", "discard-items")) {
            if (auto text = discard->value(); text == xpathPrefix || text.starts_with(withSlash) || text.starts_with(withBracket)) {
                res.emplace_back(*discard);
            }
        }
        if (auto next = discard->nextSibling()) {
            discard = next->asOpaque();
        } else {
            break;
        }
    }
    return res;
}

/**
 * @short Remove a node from a forest of tree nodes while modifying the root in-place
 */
void unlinkFromForest(std::optional<libyang::DataNode>& root, libyang::DataNode node)
{
    if (node == root) {
        root = node.nextSibling();
    }
    node.unlink();
}

/**
 * @brief Converts a filter for subscribed notification from sysrepo-cpp API into an XPath for sysrepo API.
 */
std::optional<std::string> constructXPathFilter(const std::optional<SubscribedNotificationsFilter>& filter)
{
    if (!filter) {
        return std::nullopt;
    }

    if (std::holds_alternative<std::string>(*filter)) {
        return std::get<std::string>(*filter);
    }

    auto node = std::get<libyang::DataNodeAny>(*filter);
    auto value = node.releaseValue();

    if (!value) {
        return "/"; // select nothing, RFC 6241, 6.4.2
    }

    if (std::holds_alternative<libyang::DataNode>(*value)) {
        char* str;

        auto filterTree = std::get<libyang::DataNode>(*value);
        auto res = srsn_filter_subtree2xpath(libyang::getRawNode(filterTree), nullptr, &str);
        std::unique_ptr<char, decltype([](auto* p) constexpr { std::free(p); })> strDeleter(str); // pass ownership of c-string to the deleter

        throwIfError(res, "Unable to convert subtree filter to xpath");
        return str;
    }

    throw Error("Subtree filter anydata node must contain (possibly empty) libyang tree");
}
}
