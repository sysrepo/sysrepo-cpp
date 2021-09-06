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
void Session::setItem(const char* path, const char* value)
{
    auto res = sr_set_item_str(m_sess.get(), path, value, nullptr, 0);

    throwIfError(res, "Session::setItem: Couldn't set '"s + path + "' to '" + "'" + value + "'");
}

/**
 * Retrieves a value of a single element.
 *
 * Wraps `sr_get_item`.
 *
 * @param path Path of the element to be retrieved.
 * @param timeout Optional timeout for operational callback data.
 */
Value Session::getItem(const char* path, std::chrono::milliseconds timeout)
{
    sr_val_t* val;
    auto res = sr_get_item(m_sess.get(), path, timeout.count(), &val);

    throwIfError(res, "Session::getItem: Couldn't get '"s + path + "'");

    return Value{val};
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
