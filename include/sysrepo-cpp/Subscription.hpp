/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once
#include <chrono>
#include <functional>
#include <libyang-cpp/DataNode.hpp>
#include <list>
#include <memory>
#include <optional>
#include <sysrepo-cpp/Enum.hpp>
#include <variant>

struct sr_session_ctx_s;
struct sr_subscription_ctx_s;
struct sr_change_iter_s;

namespace sysrepo {
class ChangeCollection;
class Connection;
class Session;

/**
 * @brief Contains info about a change in datastore.
 *
 * The user isn't supposed to instantiate this class, instead, Session::getChanges should be used to retrieve a change.
 */
struct Change {
    /**
     * @brief Type of the operation made on #node.
     */
    ChangeOperation operation;
    /**
     * @brief The affected node.
     */
    const libyang::DataNode node;
    /**
     * @brief Contains the previous value of a node or new preceding leaf-list instance.
     *
     * This depends on #operation and the node type of #node:
     *
     * If #operation is \link sysrepo::ChangeOperation::Created Created\endlink.
     *  - if #node is a user-ordered leaf-list, #previousValue contains the value of the preceding instance of the
     *    leaf-list. In case the created instance is first in the leaf-list, #previousValue contains an empty string.
     *  - otherwise it's `std::nullopt` (if the node is created it does not have a previous value)
     *
     * If #operation is \link sysrepo::ChangeOperation::Modified Modified\endlink.
     *  - #previousValue is the previous value of #node.
     *
     * If #operation is \link sysrepo::ChangeOperation::Deleted Deleted\endlink.
     *  - #previousValue is `std::nullopt` (value of deleted node can be retrieved from #node).
     *
     * If #operation is \link sysrepo::ChangeOperation::Moved Moved\endlink.
     *  - if #node is a user-ordered leaf-list, #previousValue is the value of the new preceding instance of #node.
     *    If #node became the first instance in the leaf-list, #previousValue contains an empty string.
     */
    std::optional<std::string_view> previousValue;
    /**
     * @brief Contains the list keys predicate for the new preceding list instance.
     *
     * This depends on #operation and the node type of #node.
     *
     * If #operation is \link sysrepo::ChangeOperation::Created Created\endlink or \link sysrepo::ChangeOperation::Moved Moved\endlink:
     *  - if #node is a user-ordered list, #previousList is list keys predicate of the new preceding instance of #node.
     *    If #node became the first instance in the list, #previousValue contains an empty string.
     *
     * Otherwise #previousList is `std::nullopt`.
     */
    std::optional<std::string_view> previousList;
    /**
     * @brief Signifies whether #previousValue was a default value.
     *
     * The value depends on #operation and the node type of #node.
     *
     * If #operation is \link sysrepo::ChangeOperation::Modified Modified\endlink and #node is a leaf:
     *  - #previousDefault is `true`, if #previousValue was the default for the leaf.
     *  - #previousDefault is `false`, if #previousValue was NOT the default for the leaf.
     *
     * Otherwise #previousDefault `false`.
     */
    bool previousDefault;
};

/**
 * @brief An iterator pointing to a single change associated with a ChangeCollection.
 */
class ChangeIterator {
public:
    ChangeIterator& operator++();
    ChangeIterator operator++(int);
    const Change& operator*() const;
    const Change& operator->() const;
    bool operator==(const ChangeIterator& other) const;

private:
    /**
     * A tag used for creating an `end` iterator.
     * Internal use only.
     */
    struct iterator_end_tag{
    };

    ChangeIterator(sr_change_iter_s* iter, std::shared_ptr<sr_session_ctx_s> sess);
    ChangeIterator(const iterator_end_tag);
    friend ChangeCollection;

    std::optional<Change> m_current;

    std::shared_ptr<sr_change_iter_s> m_iter;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};

/**
 * @brief An iterable collection containing changes to a datastore.
 *
 * This collection can be retrieved via Session::getChanges. It is compatible with range-based for-loops. Typical usage
 * of this class looks like this:
 * ```
 * // Inside a module change callback
 * for (const auto& change : session.getChanges()) {
 *     std::cerr << "Path of changed node: " << change.node.path() << "\n";
 *     // react to the changes...
 * }
 * ```
 */
class ChangeCollection {
public:
    ChangeIterator begin() const;
    ChangeIterator end() const;

private:
    ChangeCollection(const char* xpath, std::shared_ptr<sr_session_ctx_s> sess);
    friend Session;
    std::string m_xpath;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};

/**
 * Timestamp used in notification callbacks. Corresponds to the time when the notification was created.
 */
using NotificationTimeStamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;

/**
 * A callback type for module change subscriptions.
 * @param session An implicit session for the callback.
 * @param subscriptionId ID the subscription associated with the callback.
 * @param moduleName The module name used for subscribing.
 * @param subXPath The optional xpath used at the time of subscription.
 * @param event Type of the event that has occured.
 * @param requestId Request ID unique for the specific module_name. Connected events for one request (SR_EV_CHANGE and
 * SR_EV_DONE, for example) have the same request ID.
 */
using ModuleChangeCb = std::function<ErrorCode(Session session, uint32_t subscriptionId, std::string_view moduleName, std::optional<std::string_view> subXPath, Event event, uint32_t requestId)>;

/**
 * A callback for OperGet subscriptions.
 * @param session An implicit session for the callback.
 * @param subscriptionId ID the subscription associated with the callback.
 * @param moduleName The module name used for subscribing.
 * @param subXPath The optional xpath used at the time of subscription.
 * @param requestId Request ID unique for the specific module_name. Connected events for one request (SR_EV_CHANGE and
 * @param output A handle to a tree. The callback is supposed to fill this tree with the requested data.
 */
using OperGetCb = std::function<ErrorCode(Session session, uint32_t subscriptionId, std::string_view moduleName, std::optional<std::string_view> subXPath, std::optional<std::string_view> requestXPath, uint32_t requestId, std::optional<libyang::DataNode>& output)>;

/**
 * A callback for RPC/action subscriptions.
 * @param session An implicit session for the callback.
 * @param subscriptionId ID the subscription associated with the callback.
 * @param path Path identifying the RPC/action.
 * @param input Data tree specifying the input of the RPC/action.
 * @param requestId Request ID unique for the specific module_name. Connected events for one request (SR_EV_CHANGE and
 * @param output A handle to a tree. The callback is supposed to fill this tree with output data (if there are any).
 * Points to the operation root node.
 */
using RpcActionCb = std::function<ErrorCode(Session session, uint32_t subscriptionId, std::string_view path, const libyang::DataNode input, Event event, uint32_t requestId, libyang::DataNode output)>;
/**
 * A callback for notification subscriptions.
 * @param session An implicit session for the callback.
 * @param subscriptionId ID the subscription associated with the callback.
 * @param type Type of the notification.
 * @param notificationTree The tree identifying the notification. Might be std::nullopt depending on the notification
 * type.
 * @param timestamp Time when the notification was generated.
 */
using NotifCb = std::function<void(Session session, uint32_t subscriptionId, const NotificationType type, const std::optional<libyang::DataNode> notificationTree, const NotificationTimeStamp timestamp)>;

/**
 * Exception handler type for handling exceptions thrown in user callbacks.
 */
using ExceptionHandler = std::function<void(std::exception& ex)>;

/**
 * @brief For internal use only.
 */
template <typename Callback>
struct PrivData {
    Callback callback;
    ExceptionHandler* exceptionHandler;
};

template<typename Callback> PrivData(Callback, std::function<void(std::exception& ex)>*) -> PrivData<Callback>;

/**
 * @brief Contains callback for registering a Subscription to a custom event loop.
 */
struct FDHandling {
    /**
     * Called on the construction of the Subscription class.
     * This function is supposed to register polling of file descriptor `fd`. When reading is available on the file
     * descriptor, the user code should call Subscription::processEvents.
     */
    std::function<void(int fd)> registerFd;
    /**
     * Called on the destruction of the Subscription class.
     * This function is supposed to unregister polling of the `fd` file descriptor.
     */
    std::function<void(int fd)> unregisterFd;
};

/**
 * @brief Manages lifetime of subscriptions.
 */
class Subscription {
public:
    ~Subscription();
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&&) noexcept;
    Subscription& operator=(Subscription&&) noexcept;

    void processEvents();

    void onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath = nullptr, uint32_t priority = 0, const SubscribeOptions opts = SubscribeOptions::Default);
    void onOperGet(const char* moduleName, OperGetCb cb, const char* xpath, const SubscribeOptions opts = SubscribeOptions::Default);
    void onRPCAction(const char* xpath, RpcActionCb cb, uint32_t priority = 0, const SubscribeOptions opts = SubscribeOptions::Default);
    void onNotification(
            const char* moduleName,
            NotifCb cb,
            const char* xpath = nullptr,
            const std::optional<NotificationTimeStamp>& startTime = std::nullopt,
            const std::optional<NotificationTimeStamp>& stopTime = std::nullopt,
            const SubscribeOptions opts = SubscribeOptions::Default);
private:
    int eventPipe() const;
    void saveContext(sr_subscription_ctx_s* ctx);

    friend Session;
    explicit Subscription(std::shared_ptr<sr_session_ctx_s> sess, ExceptionHandler handler, const std::optional<FDHandling>& callbacks);

    std::optional<FDHandling> m_customEventLoopCbs;

    // This saves the users' callbacks. The C-style callback takes addresses of these, so the addresses need to be
    // stable (therefore, we use an std::list).
    std::list<PrivData<ModuleChangeCb>> m_moduleChangeCbs;
    std::list<PrivData<OperGetCb>> m_operGetCbs;
    std::list<PrivData<RpcActionCb>> m_RPCActionCbs;
    std::list<PrivData<NotifCb>> m_notificationCbs;

    // Need a stable address, so need to save it on the heap.
    std::shared_ptr<ExceptionHandler> m_exceptionHandler;

    std::shared_ptr<sr_session_ctx_s> m_sess;

    std::shared_ptr<sr_subscription_ctx_s> m_sub;
};
}
