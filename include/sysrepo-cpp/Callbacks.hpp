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
#include <memory>
#include <optional>
#include <sysrepo-cpp/Enum.hpp>

namespace sysrepo {
class Session;

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
using ModuleChangeCb = std::function<ErrorCode(Session session, uint32_t subscriptionId, const std::string& moduleName, const std::optional<std::string>& subXPath, Event event, uint32_t requestId)>;

/**
 * A callback for OperGet subscriptions.
 * @param session An implicit session for the callback.
 * @param subscriptionId ID the subscription associated with the callback.
 * @param moduleName The module name used for subscribing.
 * @param subXPath The optional xpath used at the time of subscription.
 * @param requestId Request ID unique for the specific module_name. Connected events for one request (SR_EV_CHANGE and
 * @param output A handle to a tree. The callback is supposed to fill this tree with the requested data.
 */
using OperGetCb = std::function<ErrorCode(Session session, uint32_t subscriptionId, const std::string& moduleName, const std::optional<std::string>& subXPath, const std::optional<std::string>& requestXPath, uint32_t requestId, std::optional<libyang::DataNode>& output)>;

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
using RpcActionCb = std::function<ErrorCode(Session session, uint32_t subscriptionId, const std::string& path, const libyang::DataNode input, Event event, uint32_t requestId, libyang::DataNode output)>;
/**
 * A callback for notification subscriptions.
 * @param session An implicit session for the callback.
 * @param subscriptionId ID the subscription associated with the callback.
 * @param type Type of the notification.
 * @param notificationTree The tree identifying the notification. For events with no matching YANG-level notification
 * (i.e., neither realtime not replay notification), std::nullopt.
 *
 * @param timestamp Time when the notification was generated.
 */
using NotifCb = std::function<void(Session session, uint32_t subscriptionId, const NotificationType type, const std::optional<libyang::DataNode> notificationTree, const NotificationTimeStamp timestamp)>;

/**
 * A callback for YANG push notification subscriptions.
 * @param notification The notification tree.
 * @param timestamp Time when the notification was generated.
 */
using YangPushNotifCb = std::function<void(const std::optional<libyang::DataNode> notificationTree, const NotificationTimeStamp timestamp)>;

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
     * This function is supposed to register polling of file descriptor `fd` and save the `processEvents` callback. When
     * reading is available on the file descriptor, the user code should call the `processEvents` callback.
     */
    std::function<void(int fd, std::function<void()> processEvents)> registerFd;
    /**
     * Called on the destruction of the Subscription class.
     * This function is supposed to unregister polling of the `fd` file descriptor.
     */
    std::function<void(int fd)> unregisterFd;
};

enum class SyncOnStart : bool {
    No,
    Yes,
};
}
