/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <cassert>
extern "C" {
#include <sysrepo.h>
}
#include <libyang-cpp/Context.hpp>
#include <sysrepo-cpp/Session.hpp>
#include <sysrepo-cpp/Subscription.hpp>
#include "utils/enum.hpp"
#include "utils/exception.hpp"

using namespace std::string_literals;
namespace sysrepo {

Session::Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn)
    // The connection `conn` is saved here in the deleter (as a capture). This means that copies of this shared_ptr will
    // automatically hold a reference to `conn`.
    : m_sess(sess, [conn] (auto* sess) {
        sr_session_stop(sess);
    })
{
}

/**
 * Constructs an unmanaged sysrepo session. Internal use only.
 */
Session::Session(sr_session_ctx_s* unmanagedSession, const unmanaged_tag)
    : m_sess(unmanagedSession, [] (sr_session_ctx_s*) {})
{
}

Datastore Session::activeDatastore() const
{
    return static_cast<Datastore>(sr_session_get_ds(m_sess.get()));
}

void Session::switchDatastore(const Datastore ds) const
{
    auto res = sr_session_switch_ds(m_sess.get(), toDatastore(ds));
    throwIfError(res, "Couldn't switch datastore");
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

    throwIfError(res, "Session::setItem: Couldn't set '"s + path + (value ? ("' to '"s + "'" + value + "'") : ""));
}

/**
 * Delete a leaf, leaf-list, list or a presence container. The changes are applied only after calling
 * Session::applyChanges.
 *
 * Wraps `sr_delete_item`.
 *
 * @param path Path of the element to be deleted.
 * @param opts Options changing the behavior of this method.
 */
void Session::deleteItem(const char* path, const EditOptions opts)
{
    auto res = sr_delete_item(m_sess.get(), path, toEditOptions(opts));

    throwIfError(res, "Session::deleteItem: Can't delete '"s + path + "'");
}

// TODO: add doc
void Session::moveItem(const char* path, const MovePosition move, const char* keys_or_value, const char* origin, const EditOptions opts)
{
    // sr_move_item has separate arguments for list keys and leaf-list values, but the C++ api has just one. It is OK if
    // both of the arguments are the same. https://github.com/sysrepo/sysrepo/issues/2621
    auto res = sr_move_item(m_sess.get(), path, toMovePosition(move), keys_or_value, keys_or_value, origin, toEditOptions(opts));

    throwIfError(res, "Session::moveItem: Can't move '"s + path + "'");
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
std::optional<libyang::DataNode> Session::getData(const char* path) const
{
    lyd_node* node;
    auto res = sr_get_data(m_sess.get(), path, 0, 0, 0, &node);

    throwIfError(res, "Session::getData: Couldn't get '"s + path + "'");

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

/**
 * Discards changes made in this Session.
 *
 * Wraps `sr_discard_changes`.
 */
void Session::discardChanges()
{
    auto res = sr_discard_changes(m_sess.get());

    throwIfError(res, "Session::discardChanges: Couldn't discard changes");
}

/**
 * Replaces configuration from `source` datastore to the current datastore. If `moduleName` is specified, the operation
 * is limited to that module. Optionally, a timeout can be specified, otherwise the default is used.
 */
void Session::copyConfig(const Datastore source, const char* moduleName, std::chrono::milliseconds timeout)
{
    auto res = sr_copy_config(m_sess.get(), moduleName, toDatastore(source), timeout.count());

    throwIfError(res, "Couldn't copy config");
}

libyang::DataNode Session::sendRPC(libyang::DataNode input, std::chrono::milliseconds timeout)
{
    lyd_node* output;
    auto res = sr_rpc_send_tree(m_sess.get(), libyang::getRawNode(input), timeout.count(), &output);
    throwIfError(res, "Couldn't send RPC");

    assert(output); // TODO: sysrepo always gives the RPC node? (even when it has not output or output nodes?)
    return libyang::wrapRawNode(output);
}

Subscription Session::onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath, uint32_t priority, const SubscribeOptions opts)
{
    auto sub = Subscription{m_sess};
    sub.onModuleChange(moduleName, cb, xpath, priority, opts);
    return sub;
}

Subscription Session::onOperGetItems(const char* moduleName, OperGetItemsCb cb, const char* xpath, const SubscribeOptions opts)
{
    auto sub = Subscription{m_sess};
    sub.onOperGetItems(moduleName, cb, xpath, opts);
    return sub;
}

Subscription Session::onRPCAction(const char* xpath, RpcActionCb cb, uint32_t priority, const SubscribeOptions opts)
{
    auto sub = Subscription{m_sess};
    sub.onRPCAction(xpath, cb, priority, opts);
    return sub;
}

/**
 * Returns a collection of changes based on an `xpath`. Use "//." to get a full change subtree.
 *
 * @param xpath XPath selecting the changes. The default selects all changes, possibly including those you didn't
 * subscribe to.
 */
ChangeCollection Session::getChanges(const char* xpath)
{
    return ChangeCollection{xpath, m_sess};
}

const libyang::Context Session::getContext() const
{
    auto ctx = sr_get_context(sr_session_get_connection(m_sess.get()));
    return libyang::createUnmanagedContext(const_cast<ly_ctx*>(ctx));
}
}
