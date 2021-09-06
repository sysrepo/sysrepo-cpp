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
#include <sysrepo-cpp/Session.hpp>
#include "utils/exception.hpp"

using namespace std::string_literals;
namespace sysrepo {

Session::Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn)
    : m_conn(conn)
    , m_sess(sess, sr_session_stop)
{
}

Datastore Session::activeDatastore() const
{
    return static_cast<Datastore>(sr_session_get_ds(m_sess.get()));
}

/**
 * Set a value of leaf, leaf-list, or create a list or a presence container. The changes are applied only after calling
 * Session::applyChanges.
 *
 * Wraps `sr_set_item_str`.
 *
 * @param path Path of the element to be changed.
 * @param value Value of the element to be changed. Can be `nullptr`.
 */
void Session::setItem(std::string_view path, std::string_view value)
{
    auto res = sr_set_item_str(m_sess.get(), path.data(), value.data(), nullptr, 0);

    throwIfError(res, "Session::setItem: Couldn't set '"s + path.data() + "' to '" + "'" + value.data() + "'");
}

/**
 * Retrieves a tree specified by the provided XPath.
 *
 * Wraps `sr_get_data`.
 *
 * @param path Path of the element to be retrieved.
 *
 * @returns std::nullopt if no matching data found, otherwise the requested data.
 */
std::optional<libyang::DataNode> Session::getData(std::string_view path)
{
    lyd_node* node;
    auto res = sr_get_data(m_sess.get(), path.data(), 0, 0, 0, &node);

    throwIfError(res, "Session::getData: Couldn't get '"s + path.data() + "'");

    if (!node) {
        return std::nullopt;
    }

    return libyang::wrapRawNode(node);
}

/**
 * Applies changes made in this Session.
 *
 * Wraps `sr_apply_changes`.
 * @param timeout Optional timeout for change callbacks.
 */
void Session::applyChanges(std::chrono::milliseconds timeout)
{
    auto res = sr_apply_changes(m_sess.get(), timeout.count());

    throwIfError(res, "Session::applyChanges: Couldn't apply changes");
}
}
