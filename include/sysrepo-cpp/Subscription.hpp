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
#include <sysrepo-cpp/Callbacks.hpp>
#include <sysrepo-cpp/Session.hpp>

struct sr_subscription_ctx_s;

namespace sysrepo {

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

    void onModuleChange(const std::string& moduleName, ModuleChangeCb cb, const std::optional<std::string>& xpath = std::nullopt, uint32_t priority = 0, const SubscribeOptions opts = SubscribeOptions::Default);
    void onOperGet(const std::string& moduleName, OperGetCb cb, const std::string& path, const SubscribeOptions opts = SubscribeOptions::Default);
    void onRPCAction(const std::string& xpath, RpcActionCb cb, uint32_t priority = 0, const SubscribeOptions opts = SubscribeOptions::Default);
    void onNotification(
            const std::string& moduleName,
            NotifCb cb,
            const std::optional<std::string>& xpath = std::nullopt,
            const std::optional<NotificationTimeStamp>& startTime = std::nullopt,
            const std::optional<NotificationTimeStamp>& stopTime = std::nullopt,
            const SubscribeOptions opts = SubscribeOptions::Default);
private:
    int eventPipe() const;
    void saveContext(sr_subscription_ctx_s* ctx);

    friend Session;
    explicit Subscription(Session sess, ExceptionHandler handler, const std::optional<FDHandling>& callbacks);

    std::optional<FDHandling> m_customEventLoopCbs;

    // This saves the users' callbacks. The C-style callback takes addresses of these, so the addresses need to be
    // stable (therefore, we use an std::list).
    std::list<PrivData<ModuleChangeCb>> m_moduleChangeCbs;
    std::list<PrivData<OperGetCb>> m_operGetCbs;
    std::list<PrivData<RpcActionCb>> m_RPCActionCbs;
    std::list<PrivData<NotifCb>> m_notificationCbs;

    // Need a stable address, so need to save it on the heap.
    std::shared_ptr<ExceptionHandler> m_exceptionHandler;

    Session m_sess;

    std::shared_ptr<sr_subscription_ctx_s> m_sub;

    bool m_didNacmInit;
};

/**
 * @brief Manages lifetime of YANG push subscriptions.
 *
 * Users are supposed to create instances of this class via Session::yangPushPeriodic, Session::yangPushOnChange or Session::subscribeNotifications.
 * Whenever notified about a change (by polling the file descriptor obtained by fd() function),
 * there is at least one event waiting to be processed by a call to YangPushSubscription::processEvent.
 *
 * Internally, the sysrepo C library creates some background thread(s). These are used either for managing internal,
 * sysrepo-level module subscriptions, or for scheduling of periodic timers. These threads are fully encapsulated by
 * the C code, and there is no control over them from this C++ wrapper. The public interface of this class is a file
 * descriptor that the caller is expected to poll for readability/closing (and the subscription ID). Once the FD is
 * readable, invoke this class' processEvents(). There is no automatic event loop which would take care of this
 * functionality, and users are expected to integrate this FD into their own event handling.
 */
class DynamicSubscription {
public:
    DynamicSubscription(const DynamicSubscription&) = delete;
    DynamicSubscription& operator=(const DynamicSubscription&) = delete;
    DynamicSubscription(DynamicSubscription&&) noexcept;
    DynamicSubscription& operator=(DynamicSubscription&&) noexcept;
    ~DynamicSubscription();

    sysrepo::Session getSession() const;
    int fd() const;
    uint64_t subscriptionId() const;
    std::optional<NotificationTimeStamp> replayStartTime() const;
    void processEvent(YangPushNotifCb cb) const;
    void terminate(const std::optional<std::string>& reason = std::nullopt);
    void modifyStopTime(const std::optional<NotificationTimeStamp>& stopTime);
    void modifyFilter(const std::optional<SubscribedNotificationsFilter>& filter);
    void modifyYangPushPeriodic(const std::chrono::milliseconds period, const std::optional<NotificationTimeStamp>& anchorTime);
    void modifyYangPushOnChange(const std::chrono::milliseconds dampeningPeriod);

private:
    DynamicSubscription(sysrepo::Session sess, int fd, uint64_t subId, const std::optional<NotificationTimeStamp>& replayStartTime = std::nullopt);

    struct Data;
    std::unique_ptr<Data> m_data;

    friend class Session;
};
}
