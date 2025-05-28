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
#include <sysrepo/netconf_acm.h>
#include <sysrepo/error_format.h>
#include <sysrepo/subscribed_notifications.h>
}
#include <libyang-cpp/Context.hpp>
#include <sysrepo-cpp/Changes.hpp>
#include <sysrepo-cpp/Connection.hpp>
#include <span>
#include <sysrepo-cpp/Subscription.hpp>
#include <utility>
#include "utils/enum.hpp"
#include "utils/exception.hpp"
#include "utils/utils.hpp"

using namespace std::string_literals;
namespace sysrepo {

namespace {
libyang::DataNode wrapSrData(std::shared_ptr<sr_session_ctx_s> sess, sr_data_t* data)
{
    // Since the lyd_node came from sysrepo and it is wrapped in a sr_data_t, we have to postpone calling the
    // sr_release_data() until after we're "done" with the libyang::DataNode.
    //
    // Normally, sr_release_data() would free the lyd_data as well. However, it is possible that the user wants to
    // manipulate the data tree (think unlink()) in a way which might have needed to overwrite the tree->data pointer.
    // Just delegate all the freeing to the C++ wrapper around lyd_data. The sysrepo library doesn't care about this.
    auto tree = std::exchange(data->tree, nullptr);

    // Use wrapRawNode, not wrapUnmanagedRawNode because we want to let the C++ wrapper manage memory.
    // Note: We're capturing the session inside the lambda.
    return libyang::wrapRawNode(tree, std::shared_ptr<sr_data_t>(data, [extend_session_lifetime = sess] (sr_data_t* data) {
        sr_release_data(data);
    }));
}

std::optional<std::string> constructXPathFilter(const std::optional<std::variant<std::string, libyang::DataNodeAny>>& filter)
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

/**
 * Wraps a pointer to sr_session_ctx_s and manages the lifetime of it. Also extends the lifetime of the connection
 * specified by the `conn` argument.
 *
 * Internal use only.
 */
Session::Session(sr_session_ctx_s* sess, Connection conn)
    : m_conn(std::move(conn))
    // The connection is saved here in the deleter (as a capture). This means that copies of this shared_ptr will
    // automatically hold a reference to `m_conn`.
    , m_mtx(std::make_shared<mutex_type>())
    // The connection `conn` is saved here in the deleter (as a capture). This means that copies of this shared_ptr will
    // automatically hold a reference to `conn`.
    , m_sess(sess, [extend_connection_lifetime = conn, mtx = this->m_mtx] (auto* sess) {
        auto guard = lock_type{*mtx};
        sr_session_stop(sess);
    })
{
}

/**
 * Constructs an unmanaged sysrepo session. Internal use only.
 */
Session::Session(sr_session_ctx_s* unmanagedSession, const unmanaged_tag)
    : m_conn(std::shared_ptr<sr_conn_ctx_s>{sr_session_get_connection(unmanagedSession), [] (sr_conn_ctx_s*) {}})
    , m_mtx(std::make_shared<mutex_type>())
    , m_sess(unmanagedSession, [mtx = this->m_mtx] (sr_session_ctx_s*) {
        auto guard = lock_type{*mtx};
    })
{
}

#define SYSREPO_CPP_ACTIVE_DATASTORE_IMPL(SESS) static_cast<Datastore>(sr_session_get_ds(SESS.get()))

/**
 * Retrieves the current active datastore.
 *
 * Wraps `sr_session_get_ds`.
 */
Datastore Session::activeDatastore() const
{
    SYSREPO_CPP_SESSION_MTX;
    return SYSREPO_CPP_ACTIVE_DATASTORE_IMPL(m_sess);
}

/**
 * Sets a new active datastore. All subsequent actions will apply to this new datastore. Previous actions won't be
 * affected.
 *
 * Wraps `sr_session_switch_ds`.
 */
void Session::switchDatastore(const Datastore ds) const
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_session_switch_ds(m_sess.get(), toDatastore(ds));
    throwIfError(res, "Couldn't switch datastore", m_sess.get());
}

/**
 * Set a value of leaf, leaf-list, or create a list or a presence container. The changes are applied only after calling
 * Session::applyChanges.
 *
 * Wraps `sr_set_item_str`.
 *
 * @param path Path of the element to be changed.
 * @param value Value of the element to be changed. Can be `std::nullopt`.
 */
void Session::setItem(const std::string& path, const std::optional<std::string>& value, const EditOptions opts)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_set_item_str(m_sess.get(), path.c_str(), value ? value->c_str() : nullptr, nullptr, toEditOptions(opts));

    throwIfError(res, "Session::setItem: Couldn't set '"s + path + "'"s + (value ? (" to '"s + *value + "'") : ""), m_sess.get());
}

/**
 * Add a prepared edit data tree to be applied. The changes are applied only after calling Session::applyChanges.
 *
 * Wraps `sr_edit_batch`.
 *
 * @param edit Data to apply.
 * @param op Default operation for nodes that do not have an operation specified. To specify the operation on a given
 * node, use libyang::DataNode::newMeta.
 */
void Session::editBatch(libyang::DataNode edit, const DefaultOperation op)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_edit_batch(m_sess.get(), libyang::getRawNode(edit), toDefaultOperation(op));

    throwIfError(res, "Session::editBatch: Couldn't apply the edit batch", m_sess.get());
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
void Session::deleteItem(const std::string& path, const EditOptions opts)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_delete_item(m_sess.get(), path.c_str(), toEditOptions(opts));

    throwIfError(res, "Session::deleteItem: Can't delete '"s + path + "'", m_sess.get());
}

/**
 * Prepare to drop "earlier content" from other sources in the operational DS for nodes matching the specified XPath
 *
 * The "earlier content" might come from the `running` datastore, or be pushed into the `operational` DS from
 * another session, with a lower priority. This function prepares a special node into the current session's
 * stored edit which effectively discards any matching content from previous, lower-priority sources.
 *
 * This function cannot be used to remove an edit which was pushed via the current session. To do that,
 * use discardChanges(), or retrieve the stored edit and manipulate its libyang data tree.
 *
 * The changes are applied only after calling Session::applyChanges.
 *
 * Wraps `sr_discard_items`.
 *
 * @param xpath Expression filtering the nodes to discard, nullopt for all nodes.
 * @see findMatchingDiscard()
 */
void Session::dropForeignOperationalContent(const std::optional<std::string>& xpath)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_discard_items(m_sess.get(), xpath ? xpath->c_str() : nullptr);

    throwIfError(res, "Session::discardItems: Can't discard "s + (xpath ? "'"s + *xpath + "'" : "all nodes"s), m_sess.get());
}

/** @short Get a copy of the stored push-operational data for this session
 *
 * To modify the stored push operational data, modify this tree in-place and pass it to editBatch()
 * with the "replace" operation.
 *
 * Wraps `sr_get_oper_changes`.
 *
 * @see findMatchingDiscard()
 */
std::optional<libyang::DataNode> Session::operationalChanges(const std::optional<std::string>& moduleName) const
{
    SYSREPO_CPP_SESSION_MTX;
    sr_data_t* data;
    auto res = sr_get_oper_changes(m_sess.get(), moduleName ? moduleName->c_str() : nullptr, &data);

    throwIfError(res, "Session::operationalChanges: Couldn't retrieve data'"s + (moduleName ? " for \"" + *moduleName + '"' : ""s), m_sess.get());

    if (!data) {
        return std::nullopt;
    }

    return wrapSrData(m_sess, data);

}

/** @short Discard push operational changes of a module for this session
 *
 * Wraps `sr_discard_oper_changes`.
 *
 * */
void Session::discardOperationalChanges(const std::optional<std::string>& moduleName, std::chrono::milliseconds timeout)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_discard_oper_changes(nullptr, m_sess.get(), moduleName ? nullptr : moduleName->c_str(), timeout.count());
    throwIfError(res, "Session::discardOoperationalChanges: Couldn't discard "s + (moduleName ? "for module \"" + *moduleName + "\"" : "globally"s), m_sess.get());
}

/**
 * Moves item (a list or a leaf-list) specified by `path`.
 * @param path Node to move.
 * @param move Specifies the type of the move.
 * @param keys_or_value list instance specified on the format [key1="val1"][key2="val2"] or a leaf-list value. Can be
 * std::nullopt for the `First` `Last` move types.
 * @param origin Origin of the value.
 * @param opts Options modifying the behavior of this method.
 */
void Session::moveItem(const std::string& path, const MovePosition move, const std::optional<std::string>& keys_or_value, const std::optional<std::string>& origin, const EditOptions opts)
{
    SYSREPO_CPP_SESSION_MTX;
    // sr_move_item has separate arguments for list keys and leaf-list values, but the C++ api has just one. It is OK if
    // both of the arguments are the same. https://github.com/sysrepo/sysrepo/issues/2621
    auto res = sr_move_item(m_sess.get(), path.c_str(), toMovePosition(move),
            keys_or_value ? keys_or_value->c_str() : nullptr,
            keys_or_value ? keys_or_value->c_str() : nullptr,
            origin ? origin->c_str() : nullptr,
            toEditOptions(opts));

    throwIfError(res, "Session::moveItem: Can't move '"s + path + "'", m_sess.get());
}

/**
 * @brief Returns a tree which contains all nodes that match the provided XPath.
 *
 * The method always returns a tree that corresponds to the requested XPath. This includes all needed parents of nodes.
 * Also, the returned node is always the first top-level node of the data returned. If one wants to access another, the
 * `findPath` method should be used on the returned data.
 *
 * Read the documentation of the wrapped C function `sr_get_data()` for additional info on XPath handling. Notably, it
 * is often a mistake to use the `//.` XPath construct for this method.
 *
 * Wraps `sr_get_data`.
 *
 * @param path XPath which corresponds to the data that should be retrieved.
 * @param maxDepth Maximum depth of the selected subtrees. 0 is unlimited, 1 will not return any descendant nodes.
 * @param opts GetOptions overriding default behaviour
 * @param timeout Optional timeout.
 * @returns std::nullopt if no matching data found, otherwise the requested data.
 */
std::optional<libyang::DataNode> Session::getData(const std::string& path, int maxDepth, const GetOptions opts, std::chrono::milliseconds timeout) const
{
    SYSREPO_CPP_SESSION_MTX;
    sr_data_t* data;
    auto res = sr_get_data(m_sess.get(), path.c_str(), maxDepth, timeout.count(), toGetOptions(opts), &data);

    throwIfError(res, "Session::getData: Couldn't get '"s + path + "'", m_sess.get());

    if (!data) {
        return std::nullopt;
    }

    return wrapSrData(m_sess, data);
}

/**
 * @brief Returns a single value matching the provided XPath.
 *
 * The resulting DataNode is *disconnected* from its parent(s).
 * This has some implications.
 * For more details, please refer to the libyang C documentation.
 *
 * If there's no match, this throws ErrorWithCode(..., SR_ERR_NOT_FOUND).
 *
 * Wraps `sr_get_node`.
 *
 * @param path XPath which corresponds to the data that should be retrieved.
 * @param timeout Optional timeout.
 * @returns the requested data node (as a disconnected node)
 */
libyang::DataNode Session::getOneNode(const std::string& path, std::chrono::milliseconds timeout) const
{
    SYSREPO_CPP_SESSION_MTX;
    sr_data_t* data;
    auto res = sr_get_node(m_sess.get(), path.c_str(), timeout.count(), &data);

    throwIfError(res, "Session::getOneNode: Couldn't get '"s + path + "'", m_sess.get());

    return wrapSrData(m_sess, data);
}

/**
 * @brief Retrieves changes that have not been applied yet.
 *
 * Do NOT change the returned data. It is possible to duplicate them. After the changes get applied or discarded, they
 * become INVALID.
 *
 * @return The pending data, or std::nullopt if there are none.
 */
std::optional<const libyang::DataNode> Session::getPendingChanges() const
{
    SYSREPO_CPP_SESSION_MTX;
    auto changes = sr_get_changes(m_sess.get());
    if (!changes) {
        return std::nullopt;
    }

    return libyang::wrapUnmanagedRawNode(changes);
}

/**
 * Applies changes made in this Session.
 *
 * Wraps `sr_apply_changes`.
 * @param timeout Optional timeout for change callbacks.
 */
void Session::applyChanges(std::chrono::milliseconds timeout)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_apply_changes(m_sess.get(), timeout.count());

    throwIfError(res, "Session::applyChanges: Couldn't apply changes", m_sess.get());
}

/**
 * Discards changes made earlier in this Session, optionally only below a given XPath
 *
 * The changes are applied only after calling Session::applyChanges.
 *
 * Wraps `sr_discard_changes_xpath`.
 */
void Session::discardChanges(const std::optional<std::string>& xpath)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_discard_changes_xpath(m_sess.get(), xpath ? xpath->c_str() : nullptr);

    throwIfError(res, "Session::discardChanges: Couldn't discard changes", m_sess.get());
}

/**
 * Replaces configuration from `source` datastore to the current datastore. If `moduleName` is specified, the operation
 * is limited to that module. Optionally, a timeout can be specified, otherwise the default is used.
 *
 * Wraps `sr_copy_config`.
 *
 * @param The source datastore.
 * @optional moduleName Optional module name, limits the operation on that module.
 * @optional timeout Optional timeout.
 */
void Session::copyConfig(const Datastore source, const std::optional<std::string>& moduleName, std::chrono::milliseconds timeout)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_copy_config(m_sess.get(), moduleName ? moduleName->c_str() : nullptr, toDatastore(source), timeout.count());

    throwIfError(res, "Couldn't copy config", m_sess.get());
}

/**
 * Send an RPC/action and return the result.
 *
 * Wraps `sr_rpc_send_tree`.
 *
 * @param input Libyang tree representing the RPC/action.
 * @param timeout Optional timeout.
 */
std::optional<libyang::DataNode> Session::sendRPC(libyang::DataNode input, std::chrono::milliseconds timeout)
{
    SYSREPO_CPP_SESSION_MTX;
    sr_data_t* output;
    auto res = sr_rpc_send_tree(m_sess.get(), libyang::getRawNode(input), timeout.count(), &output);
    throwIfError(res, "Couldn't send RPC", m_sess.get());

    if (!output) {
        return std::nullopt;
    }
    return wrapSrData(m_sess, output);
}

/**
 * Send a notification.
 *
 * Wraps `sr_notif_send_tree`.
 *
 * @param notification Libyang tree representing the notification.
 * @param wait Specifies whether to wait until all (if any) notification callbacks were called.
 * @param timeout Optional timeout. Only meaningful if we're waiting for the notification callbacks.
 */
void Session::sendNotification(libyang::DataNode notification, const Wait wait, std::chrono::milliseconds timeout)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_notif_send_tree(m_sess.get(), libyang::getRawNode(notification), timeout.count(), wait == Wait::Yes ? 1 : 0);
    throwIfError(res, "Couldn't send notification", m_sess.get());
}


/**
 * Replace datastore's content with the provided data
 *
 * Wraps `sr_replace_config`.
 *
 * @param config Libyang tree to use as a complete datastore content, or nullopt
 * @param moduleName If provided, a module name to limit the operation to
 * @param timeout Optional timeout to wait for
 */
void Session::replaceConfig(std::optional<libyang::DataNode> config, const std::optional<std::string>& moduleName, std::chrono::milliseconds timeout)
{
    SYSREPO_CPP_SESSION_MTX;
    std::optional<libyang::DataNode> thrashable;
    if (config) {
        thrashable = config->duplicateWithSiblings(libyang::DuplicationOptions::Recursive | libyang::DuplicationOptions::WithParents);
    }
    auto res = sr_replace_config(
        m_sess.get(), moduleName ? moduleName->c_str() : nullptr,
        config ? libyang::releaseRawNode(*thrashable) : nullptr,
        timeout.count());
    throwIfError(res, "sr_replace_config failed", m_sess.get());
}

/**
 * Subscribe for changes made in the specified module.
 *
 * Wraps `sr_module_change_subscribe`.
 *
 * @param moduleName Name of the module to suscribe to.
 * @param cb A callback to be called when a change in the datastore occurs.
 * @param xpath Optional XPath that filters changes handled by this subscription.
 * @param priority Optional priority in which the callbacks within a module are called.
 * @param opts Options further changing the behavior of this method.
 * @param handler Optional exception handler that will be called when an exception occurs in a user callback. It is tied
 * to all of the callbacks in a Subscription instance.
 * @param callbacks Custom event loop callbacks that are called when the Subscription is created and when it is
 * destroyed. This argument must be used with `sysrepo::SubscribeOptions::NoThread` flag.
 *
 * @return The Subscription handle.
 */
Subscription Session::onModuleChange(
        const std::string& moduleName,
        ModuleChangeCb cb,
        const std::optional<std::string>& xpath,
        uint32_t priority,
        const SubscribeOptions opts,
        ExceptionHandler handler,
        const std::optional<FDHandling>& callbacks)
{
    checkNoThreadFlag(opts, callbacks);
    auto sub = Subscription{*this, handler, callbacks};
    sub.onModuleChange(moduleName, cb, xpath, priority, opts);
    return sub;
}

/**
 * Subscribe for providing operational data at the given xpath.
 *
 * Wraps `sr_oper_get_subscribe`.
 *
 * @param moduleName Name of the module to suscribe to.
 * @param cb A callback to be called when the operaional data for the given xpath are requested.
 * @param xpath XPath that identifies which data this subscription is able to provide.
 * @param opts Options further changing the behavior of this method.
 * @param handler Optional exception handler that will be called when an exception occurs in a user callback. It is tied
 * to all of the callbacks in a Subscription instance.
 * @param callbacks Custom event loop callbacks that are called when the Subscription is created and when it is
 * destroyed. This argument must be used with `sysrepo::SubscribeOptions::NoThread` flag.
 *
 * @return The Subscription handle.
 */
Subscription Session::onOperGet(
        const std::string& moduleName,
        OperGetCb cb,
        const std::optional<std::string>& xpath,
        const SubscribeOptions opts,
        ExceptionHandler handler,
        const std::optional<FDHandling>& callbacks)
{
    checkNoThreadFlag(opts, callbacks);
    auto sub = Subscription{*this, handler, callbacks};
    sub.onOperGet(moduleName, cb, xpath, opts);
    return sub;
}

/**
 * Subscribe for the delivery of an RPC/action.
 *
 * Wraps `sr_rpc_subscribe_tree`.
 *
 * @param xpath XPath identifying the RPC/action.
 * @param cb A callback to be called to handle the RPC/action.
 * @param priority Optional priority in which the callbacks within a module are called.
 * @param opts Options further changing the behavior of this method.
 * @param handler Optional exception handler that will be called when an exception occurs in a user callback. It is tied
 * to all of the callbacks in a Subscription instance.
 * @param callbacks Custom event loop callbacks that are called when the Subscription is created and when it is
 * destroyed. This argument must be used with `sysrepo::SubscribeOptions::NoThread` flag.
 *
 * @return The Subscription handle.
 */
Subscription Session::onRPCAction(
        const std::string& xpath,
        RpcActionCb cb,
        uint32_t priority,
        const SubscribeOptions opts,
        ExceptionHandler handler,
        const std::optional<FDHandling>& callbacks)
{
    checkNoThreadFlag(opts, callbacks);
    auto sub = Subscription{*this, handler, callbacks};
    sub.onRPCAction(xpath, cb, priority, opts);
    return sub;
}

/**
 * Subscribe for the delivery of a notification
 *
 * Wraps `sr_notif_subscribe`.
 *
 * @param moduleName Name of the module to suscribe to.
 * @param cb A callback to be called to process the notification.
 * @param xpath Optional XPath that filters received notification.
 * @param startTime Optional start time of the subscription. Used for replaying stored notifications.
 * @param stopTime Optional stop time ending the notification subscription.
 * @param opts Options further changing the behavior of this method.
 * @param handler Optional exception handler that will be called when an exception occurs in a user callback. It is tied
 * to all of the callbacks in a Subscription instance.
 * @param callbacks Custom event loop callbacks that are called when the Subscription is created and when it is
 * destroyed. This argument must be used with `sysrepo::SubscribeOptions::NoThread` flag.
 *
 * @return The Subscription handle.
 */
Subscription Session::onNotification(
        const std::string& moduleName,
        NotifCb cb,
        const std::optional<std::string>& xpath,
        const std::optional<NotificationTimeStamp>& startTime,
        const std::optional<NotificationTimeStamp>& stopTime,
        const SubscribeOptions opts,
        ExceptionHandler handler,
        const std::optional<FDHandling>& callbacks)
{
    checkNoThreadFlag(opts, callbacks);
    auto sub = Subscription{*this, handler, callbacks};
    sub.onNotification(moduleName, cb, xpath, startTime, stopTime, opts);
    return sub;
}

/**
 * Subscribe for receiving notifications according to 'ietf-yang-push' YANG periodic subscriptions.
 *
 * Wraps `srsn_subscribe` and `srsn_filter_subtree2xpath` for subtree filters.
 *
 * @param filter Optional filter for received notification, xpath filter for string type, subtree filter for libyang::DataNodeAny
 * @param periodTime Notification period.
 * @param anchorTime Optional anchor time for the period. Anchor time acts as a reference point for the period.
 * @param stopTime Optional stop time ending the notification subscription.
 *
 * @return A DynamicSubscription handle.
 */
DynamicSubscription Session::yangPushPeriodic(
    const std::optional<std::variant<std::string, libyang::DataNodeAny>>& filter,
    std::chrono::milliseconds periodTime,
    const std::optional<NotificationTimeStamp>& anchorTime,
    const std::optional<NotificationTimeStamp>& stopTime)
{
    SYSREPO_CPP_SESSION_MTX;
    int fd;
    uint32_t subId;
    auto stopSpec = stopTime ? std::optional{toTimespec(*stopTime)} : std::nullopt;
    auto anchorSpec = anchorTime ? std::optional{toTimespec(*anchorTime)} : std::nullopt;
    auto xpathFilter = constructXPathFilter(filter);

    auto res = srsn_yang_push_periodic(m_sess.get(),
                                       toDatastore(activeDatastore()),
                                       xpathFilter ? xpathFilter->c_str() : nullptr,
                                       periodTime.count(),
                                       anchorSpec ? &anchorSpec.value() : nullptr,
                                       stopSpec ? &stopSpec.value() : nullptr,
                                       &fd,
                                       &subId);
    throwIfError(res, "Couldn't create yang-push periodic subscription", m_sess.get());

    return {*this, fd, subId};
}

/**
 * Subscribe for receiving notifications according to 'ietf-yang-push' YANG on-change subscriptions.
 *
 * Wraps `srsn_subscribe` and `srsn_filter_subtree2xpath` for subtree filters.
 *
 * @param filter Optional filter for received notification, xpath filter for string type, subtree filter for libyang::DataNodeAny
 * @param dampeningPeriod Optional dampening period.
 * @param syncOnStart Whether to start with a notification of the current state.
 * @param stopTime Optional stop time ending the notification subscription.
 *
 * @return A DynamicSubscription handle.
 */
DynamicSubscription Session::yangPushOnChange(
    const std::optional<std::variant<std::string, libyang::DataNodeAny>>& filter,
    const std::optional<std::chrono::milliseconds>& dampeningPeriod,
    SyncOnStart syncOnStart,
    const std::set<YangPushChange>& excludedChanges,
    const std::optional<NotificationTimeStamp>& stopTime)
{
    SYSREPO_CPP_SESSION_MTX;
    int fd;
    uint32_t subId;
    auto stopSpec = stopTime ? std::optional{toTimespec(*stopTime)} : std::nullopt;
    auto xpathFilter = constructXPathFilter(filter);

    /* The enum values are not used as the other enum flags in sysrepo-cpp.
     * srsn_yang_push_on_change expects an integer array of size EnumCount with 0 or 1 values.
     */
    using YangPushChangeUnderlying = std::underlying_type_t<YangPushChange>;
    std::array<int, static_cast<YangPushChangeUnderlying>(YangPushChange::EnumCount)> excludedChangesArray{};
    for (const auto& change: excludedChanges) {
        excludedChangesArray[static_cast<YangPushChangeUnderlying>(change)] = 1;
    }

    auto res = srsn_yang_push_on_change(m_sess.get(),
                                        toDatastore(activeDatastore()),
                                        xpathFilter ? xpathFilter->c_str() : nullptr,
                                        dampeningPeriod ? dampeningPeriod->count() : 0,
                                        syncOnStart == SyncOnStart::Yes,
                                        excludedChangesArray.data(),
                                        stopSpec ? &stopSpec.value() : nullptr,
                                        0,
                                        nullptr,
                                        &fd,
                                        &subId);
    throwIfError(res, "Couldn't create yang-push on-change subscription", m_sess.get());

    return {*this, fd, subId};
}

/**
 * Subscribe for receiving notifications according to 'ietf-subscribed-notifications'.
 *
 * Wraps `srsn_subscribe` and `srsn_filter_subtree2xpath` for subtree filters.
 *
 * @param filter Optional filter for received notification, xpath filter for string type, subtree filter for libyang::DataNodeAny
 * @param stream Optional stream to subscribe to.
 * @param stopTime Optional stop time ending the subscription.
 * @param startTime Optional start time of the subscription, used for replaying stored notifications.
 *
 * @return A DynamicSubscription handle.
 */
DynamicSubscription Session::subscribeNotifications(
    const std::optional<std::variant<std::string, libyang::DataNodeAny>>& filter,
    const std::optional<std::string>& stream,
    const std::optional<NotificationTimeStamp>& stopTime,
    const std::optional<NotificationTimeStamp>& startTime)
{
    SYSREPO_CPP_SESSION_MTX;
    int fd;
    uint32_t subId;
    auto stopSpec = stopTime ? std::optional{toTimespec(*stopTime)} : std::nullopt;
    auto startSpec = startTime ? std::optional{toTimespec(*startTime)} : std::nullopt;
    struct timespec replayStartSpec;
    auto xpathFilter = constructXPathFilter(filter);

    auto res = srsn_subscribe(m_sess.get(),
                              stream ? stream->c_str() : nullptr,
                              xpathFilter ? xpathFilter->data() : nullptr,
                              stopSpec ? &stopSpec.value() : nullptr,
                              startSpec ? &startSpec.value() : nullptr,
                              false,
                              nullptr,
                              &replayStartSpec,
                              &fd,
                              &subId);

    throwIfError(res, "Couldn't create notification subscription", m_sess.get());

    std::optional<NotificationTimeStamp> replayStart;
    if (replayStartSpec.tv_sec != 0) {
        replayStart = toTimePoint(replayStartSpec);
    }

    return {*this, fd, subId, replayStart};
}

/**
 * Returns a collection of changes based on an `xpath`. Use "//." to get a full change subtree.
 *
 * @param xpath XPath selecting the changes. The default selects all changes, possibly including those you didn't
 * subscribe to.
 */
ChangeCollection Session::getChanges(const std::string& xpath)
{
    SYSREPO_CPP_SESSION_MTX;
    return ChangeCollection{xpath, *this};
}

/**
 * @brief Set the NACM user for this session, which enables NACM for all operations on this session.
 */
void Session::setNacmUser(const std::string& user)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_nacm_set_user(m_sess.get(), user.c_str());
    throwIfError(res, "Couldn't set NACM user", m_sess.get());
}

/**
 * @brief Get the NACM user for this session.
 */
std::optional<std::string> Session::getNacmUser() const
{
    SYSREPO_CPP_SESSION_MTX;
    auto* username = sr_nacm_get_user(m_sess.get());
    return username ? std::make_optional<std::string>(username) : std::nullopt;
}

/**
 * @brief Get the sysrepo NACM recovery user.
 *
 * wraps `sr_nacm_get_recovery_user`.
 */
std::string Session::getNacmRecoveryUser()
{
    return sr_nacm_get_recovery_user();
}

/**
 * @brief Checks if operation is allowed for current NACM user. Wraps `sr_nacm_check_operation`.
 * @return true if the current user is authorized to perform operation on given @p node.
 *
 * Details on unsuccessfull authorizations can be retrieved via Session::getErrors.
 * Note that if the NACM user is not set, `sr_nacm_check_operation` and this function both return true.
 */
bool Session::checkNacmOperation(const libyang::DataNode& node) const
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_nacm_check_operation(m_sess.get(), libyang::getRawNode(node));
    return res == SR_ERR_OK;
}

/**
 * @brief Initializes NACM callbacks.
 *
 * @param opts Can contain the sysrepo::SubscribeOptions::NoThread. Other flags are invalid.
 * @param handler Can contain an exception handler for additional user subscriptions.
 * @param callbacks If using sysrepo::SubscribeOptions::NoThread, this argument specifies the FD register/unregister
 * callbacks.
 * @return A Subscription that contains the NACM subscriptions. It can be used to create other subscriptions.
 */
[[nodiscard]] Subscription Session::initNacm(SubscribeOptions opts, ExceptionHandler handler, const std::optional<FDHandling>& callbacks)
{
    SYSREPO_CPP_SESSION_MTX;
    sr_subscription_ctx_t* sub = nullptr;
    auto res = sr_nacm_init(m_sess.get(), toSubscribeOptions(opts), &sub);
    throwIfError(res, "Couldn't initialize NACM", m_sess.get());

    Subscription ret(*this, handler, callbacks);
    ret.saveContext(sub);
    ret.m_didNacmInit = true;

    return ret;
}

/**
 * Sets a generic sysrepo error message.
 *
 * Wraps `sr_session_set_error_message`.
 *
 * @param msg The message to be set.
 */
void Session::setErrorMessage(const std::string& msg)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_session_set_error_message(m_sess.get(), "%s", msg.c_str());
    throwIfError(res, "Couldn't set error message");
}

/**
 * Set NETCONF callback error.
 *
 * Wraps `sr_session_set_netconf_error`.
 *
 * @param info An object that specifies the error.
 */
void Session::setNetconfError(const NetconfErrorInfo& info)
{
    SYSREPO_CPP_SESSION_MTX;
    std::vector<const char*> elements, values;
    elements.reserve(info.infoElements.size());
    values.reserve(elements.size());

    for (const auto& infoElem : info.infoElements) {
        elements.emplace_back(infoElem.element.c_str());
        values.emplace_back(infoElem.value.c_str());
    }

    auto res = sr_session_set_netconf_error2(
            m_sess.get(),
            info.type.c_str(),
            info.tag.c_str(),
            info.appTag ? info.appTag->c_str() : nullptr,
            info.path ? info.path->c_str() : nullptr,
            info.message.c_str(),
            info.infoElements.size(), elements.data(), values.data()
            );
    throwIfError(res, "Couldn't set error messsage");
}

template <typename ErrType>
std::vector<ErrType> impl_getErrors(sr_session_ctx_s* sess)
{
    const sr_error_info_t* errInfo;
    auto res = sr_session_get_error(sess, &errInfo);
    throwIfError(res, "Couldn't retrieve errors");

    std::vector<ErrType> errors;

    if (!errInfo) {
        return errors;
    }

    for (const auto& error : std::span(errInfo->err, errInfo->err_count)) {
        using namespace std::string_view_literals;
        if constexpr (std::is_same<ErrType, NetconfErrorInfo>()) {
            if (!error.error_format || error.error_format != "NETCONF"sv) {
                continue;
            }

            const char* type;
            const char* tag;
            const char* appTag;
            const char* path;
            const char* message;
            const char** infoElements;
            const char** infoValues;
            uint32_t infoCount;

            auto res = sr_err_get_netconf_error(&error, &type, &tag, &appTag, &path, &message, &infoElements, &infoValues, &infoCount);
            throwIfError(res, "Couldn't retrieve errors");

            auto& netconfErr = errors.emplace_back();
            netconfErr.type = type;
            netconfErr.tag = tag;
            if (appTag) {
                netconfErr.appTag = appTag;
            }
            if (path) {
                netconfErr.path = path;
            }
            netconfErr.message = message;
            if (infoElements) {
                using deleter_free_t = decltype([](auto x) constexpr { std::free(x); });
                auto infoElemsDeleter = std::unique_ptr<const char*, deleter_free_t>(infoElements);
                auto infoValuesDeleter = std::unique_ptr<const char*, deleter_free_t>(infoValues);
                auto elems = std::span(infoElements, infoCount);
                auto vals = std::span(infoValues, infoCount);

                for (auto [elem, val] = std::tuple{elems.begin(), vals.begin()}; elem != elems.end(); elem++, val++) {
                    netconfErr.infoElements.push_back(NetconfErrorInfo::InfoElement{*elem, *val});
                }
            }

        } else {
            static_assert(std::is_same<ErrType, ErrorInfo>());
            if (error.message) {
                // Sometimes there's no error message which would mean an invalid std::string construction.
                // In that case just skip that thing.
                errors.push_back(ErrorInfo{
                    .code = static_cast<ErrorCode>(error.err_code),
                    .errorMessage = error.message,
                });
            }
        }
    }

    return errors;
}

/**
 * Retrieve all generic sysrepo errors.
 *
 * Wraps `sr_session_get_error`.
 * @return A vector of all errors.
 */
std::vector<ErrorInfo> Session::getErrors() const
{
    SYSREPO_CPP_SESSION_MTX;
    return impl_getErrors<ErrorInfo>(m_sess.get());
}

/**
 * Retrieve all NETCONF-style errors.
 *
 * Wraps `sr_err_get_netconf_error`.
 * @return A vector of all NETCONF errors.
 */
std::vector<NetconfErrorInfo> Session::getNetconfErrors() const
{
    SYSREPO_CPP_SESSION_MTX;
    return impl_getErrors<NetconfErrorInfo>(m_sess.get());
}

std::ostream& operator<<(std::ostream& stream, const ErrorInfo& e)
{
    return stream << e.errorMessage << " (" << e.code << ")";
}

std::ostream& operator<<(std::ostream& stream, const NetconfErrorInfo& e)
{
    stream << e.type << ": " << e.tag << ": ";
    if (e.appTag) {
        stream << *e.appTag << ": ";
    }
    if (e.path) {
        stream << *e.path << ": ";
    }
    stream << e.message;
    for (const auto& info : e.infoElements) {
        stream << " \"" << info.element << "\": value \"" << info.value << "\"";
    }
    return stream;
}

/**
 * Gets the event originator name. If it hasn't been set, the name is empty.
 *
 * Wraps `sr_session_get_orig_name`.
 * @return The originator name.
 */
std::string Session::getOriginatorName() const
{
    SYSREPO_CPP_SESSION_MTX;
    return sr_session_get_orig_name(m_sess.get());
}

/**
 * Sets the event originator name.
 *
 * Wraps `sr_session_set_orig_name`.
 * @param originatorName The new originator name.
 */
void Session::setOriginatorName(const std::string& originatorName)
{
    SYSREPO_CPP_SESSION_MTX;
    auto res = sr_session_set_orig_name(m_sess.get(), originatorName.c_str());
    throwIfError(res, "Couldn't switch datastore", m_sess.get());
}

/**
 * Returns the connection this session was created on.
 */
Connection Session::getConnection()
{
    return m_conn;
}

/**
 * Returns the libyang context associated with this Session.
 * Wraps `sr_session_acquire_context`.
 * @return The context.
 */
const libyang::Context Session::getContext() const
{
    SYSREPO_CPP_SESSION_MTX;
    auto ctx = sr_session_acquire_context(m_sess.get());
    return libyang::createUnmanagedContext(const_cast<ly_ctx*>(ctx), [sess = m_sess] (ly_ctx*) { sr_session_release_context(sess.get()); });
}

/**
 * @brief Get the internal, sysrepo-level session ID
 *
 * Wraps `sr_session_get_id`.
 */
uint32_t Session::getId() const
{
    SYSREPO_CPP_SESSION_MTX;
    return sr_session_get_id(m_sess.get());
}

Lock::Lock(Session session, std::optional<std::string> moduleName, std::optional<std::chrono::milliseconds> timeout)
    : m_session(session)
    , m_lockedDs(m_session.activeDatastore())
    , m_module(moduleName)
{
    SYSREPO_CPP_SESSION_MTX_OF(m_session);
    auto res = sr_lock(getRawSession(m_session), moduleName ? moduleName->c_str() : nullptr, timeout ? timeout->count() : 0);
    throwIfError(res, "Cannot lock session", getRawSession(m_session));
}

Lock::~Lock()
{
    SYSREPO_CPP_SESSION_MTX_OF(m_session);
    auto sess = getRawSession(m_session);
    // Unlocking has to be performed in the same DS as the original locking, but the current active DS might have changed.
    // Temporary switching is safe here because these C API methods cannot fail (as of 2024-01 at least), and the C API
    // documents Session to be only usable form a single thread.
    auto currentDs = sr_session_get_ds(sess);
    sr_session_switch_ds(sess, toDatastore(m_lockedDs));
    auto res = sr_unlock(sess, m_module ? m_module->c_str() : nullptr);
    sr_session_switch_ds(sess, currentDs);
    throwIfError(res, "Cannot unlock session", sess);
}

sr_session_ctx_s* getRawSession(Session sess)
{
    return sess.m_sess.get();
}
}
