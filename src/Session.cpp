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
#include <sysrepo/error_format.h>
}
#include <libyang-cpp/Context.hpp>
#include <sysrepo-cpp/Connection.hpp>
#include <span>
#include <sysrepo-cpp/Subscription.hpp>
#include "utils/enum.hpp"
#include "utils/exception.hpp"
#include "utils/utils.hpp"

using namespace std::string_literals;
namespace sysrepo {
Session::Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn)
    // The connection `conn` is saved here in the deleter (as a capture). This means that copies of this shared_ptr will
    // automatically hold a reference to `conn`.
    : m_sess(sess, [extend_connection_lifetime = conn] (auto* sess) {
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
void Session::setItem(const char* path, const char* value, const EditOptions opts)
{
    auto res = sr_set_item_str(m_sess.get(), path, value, nullptr, toEditOptions(opts));

    throwIfError(res, "Session::setItem: Couldn't set '"s + path + (value ? ("' to '"s + "'" + value + "'") : ""));
}

void Session::editBatch(libyang::DataNode edit, const DefaultOperation op)
{
    auto res = sr_edit_batch(m_sess.get(), libyang::getRawNode(edit), toDefaultOperation(op));

    throwIfError(res, "Session::editBatch: Couldn't apply the edit batch");
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

/**
 * Moves item (a list or a leaf-list) specified by `path`.
 * @param path Node to move.
 * @param move Specifies the type of the move.
 * @param keys_or_value list instance specified on the format [key1="val1"][key2="val2"] or a leaf-list value. Can be
 * nullptr for the `First` `Last` move types.
 * @param origin Origin of the value.
 * @param opts Options modifying the behavior of this method.
 */
void Session::moveItem(const char* path, const MovePosition move, const char* keys_or_value, const char* origin, const EditOptions opts)
{
    // sr_move_item has separate arguments for list keys and leaf-list values, but the C++ api has just one. It is OK if
    // both of the arguments are the same. https://github.com/sysrepo/sysrepo/issues/2621
    auto res = sr_move_item(m_sess.get(), path, toMovePosition(move), keys_or_value, keys_or_value, origin, toEditOptions(opts));

    throwIfError(res, "Session::moveItem: Can't move '"s + path + "'");
}

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
    return libyang::wrapRawNode(tree, std::shared_ptr<sr_data_t>(data, [extend_session_lifetime = sess] (sr_data_s* data) {
        sr_release_data(data);
    }));
}
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
    sr_data_t* data;
    auto res = sr_get_data(m_sess.get(), path, 0, 0, 0, &data);

    throwIfError(res, "Session::getData: Couldn't get '"s + path + "'");

    if (!data) {
        return std::nullopt;
    }

    return wrapSrData(m_sess, data);
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
    sr_data_t* output;
    auto res = sr_rpc_send_tree(m_sess.get(), libyang::getRawNode(input), timeout.count(), &output);
    throwIfError(res, "Couldn't send RPC");

    assert(output); // TODO: sysrepo always gives the RPC node? (even when it has not output or output nodes?)
    return wrapSrData(m_sess, output);
}

void Session::sendNotification(libyang::DataNode notification, const Wait wait, std::chrono::milliseconds timeout)
{
    auto res = sr_notif_send_tree(m_sess.get(), libyang::getRawNode(notification), timeout.count(), wait == Wait::Yes ? 1 : 0);
    throwIfError(res, "Couldn't send notification");
}

Subscription Session::onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath, uint32_t priority, const SubscribeOptions opts, ExceptionHandler handler)
{
    auto sub = Subscription{m_sess, handler};
    sub.onModuleChange(moduleName, cb, xpath, priority, opts);
    return sub;
}

Subscription Session::onOperGet(const char* moduleName, OperGetCb cb, const char* xpath, const SubscribeOptions opts, ExceptionHandler handler)
{
    auto sub = Subscription{m_sess, handler};
    sub.onOperGet(moduleName, cb, xpath, opts);
    return sub;
}

Subscription Session::onRPCAction(const char* xpath, RpcActionCb cb, uint32_t priority, const SubscribeOptions opts, ExceptionHandler handler)
{
    auto sub = Subscription{m_sess, handler};
    sub.onRPCAction(xpath, cb, priority, opts);
    return sub;
}

Subscription Session::onNotification(
        const char* moduleName,
        NotifCb cb,
        const char* xpath,
        const std::optional<NotificationTimeStamp>& startTime,
        const std::optional<NotificationTimeStamp>& stopTime,
        const SubscribeOptions opts,
        ExceptionHandler handler)
{
    auto sub = Subscription{m_sess, handler};
    sub.onNotification(moduleName, cb, xpath, startTime, stopTime, opts);
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

void Session::setErrorMessage(const char* msg)
{
    auto res = sr_session_set_error_message(m_sess.get(), "%s", msg);
    throwIfError(res, "Couldn't set error message");
}

/**
 * Set NETCONF callback error.
 */
void Session::setNetconfError(const NetconfErrorInfo& info)
{
    auto res = sr_session_set_netconf_error(
            m_sess.get(),
            info.type.c_str(),
            info.tag.c_str(),
            info.appTag ? info.appTag->c_str() : nullptr,
            info.path ? info.path->c_str() : nullptr,
            info.message.c_str(), 0);
    throwIfError(res, "Couldn't set error messsage");

    // Unfortunately, there is no way to transform the vector to the variadic arguments, so I need to push the info
    // elements manually.
    for (const auto& infoElem : info.infoElements) {
        res = sr_session_push_error_data(m_sess.get(), infoElem.element.size() + /*NULL byte*/ 1, infoElem.element.c_str());
        throwIfError(res, "Couldn't set error messsage");
        res = sr_session_push_error_data(m_sess.get(), infoElem.value.size() + /*NULL byte*/ 1, infoElem.value.c_str());
        throwIfError(res, "Couldn't set error messsage");
    }
}

namespace {
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
                auto infoElemsDeleter = std::unique_ptr<const char*, decltype(&std::free)>(infoElements, std::free);
                auto infoValuesDeleter = std::unique_ptr<const char*, decltype(&std::free)>(infoValues, std::free);
                auto elems = std::span(infoElements, infoCount);
                auto vals = std::span(infoValues, infoCount);

                for (auto [elem, val] = std::tuple{elems.begin(), vals.begin()}; elem != elems.end(); elem++, val++) {
                    netconfErr.infoElements.push_back(NetconfErrorInfo::InfoElement{*elem, *val});
                }
            }

        } else  {
            if (error.error_format) {
                // Error has a special error_format, we don't know what it is!
                continue;
            }

            static_assert(std::is_same<ErrType, ErrorInfo>());
            errors.push_back(ErrorInfo{
                .code = static_cast<ErrorCode>(error.err_code),
                .errorMessage = error.message ? std::optional{error.message} : std::nullopt
            });
        }
    }

    return errors;
}
};

std::vector<ErrorInfo> Session::getErrors()
{
    return impl_getErrors<ErrorInfo>(m_sess.get());
}

std::vector<NetconfErrorInfo> Session::getNetconfErrors() const
{
    return impl_getErrors<NetconfErrorInfo>(m_sess.get());
}

const libyang::Context Session::getContext() const
{
    auto ctx = sr_session_acquire_context(m_sess.get());
    return libyang::createUnmanagedContext(const_cast<ly_ctx*>(ctx), [sess = m_sess] (ly_ctx*) { sr_session_release_context(sess.get()); });
}
}
