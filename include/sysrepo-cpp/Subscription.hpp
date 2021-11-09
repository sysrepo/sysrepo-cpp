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

struct Change {
    ChangeOperation operation;
    const libyang::DataNode node;
    std::optional<std::string_view> previousValue;
    std::optional<std::string_view> previousList;
    bool previousDefault;
};

class ChangeIterator {
public:
    struct iterator_end_tag{
    };

    ChangeIterator& operator++();
    ChangeIterator operator++(int);
    const Change& operator*() const;
    const Change& operator->() const;
    bool operator==(const ChangeIterator& other) const;

private:
    ChangeIterator(sr_change_iter_s* iter, std::shared_ptr<sr_session_ctx_s> sess);
    ChangeIterator(const iterator_end_tag);
    friend ChangeCollection;

    std::optional<Change> m_current;

    std::shared_ptr<sr_change_iter_s> m_iter;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};

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

using NotificationTimeStamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;

using ModuleChangeCb = std::function<ErrorCode(Session session, uint32_t subscriptionId, std::string_view moduleName, std::optional<std::string_view> subXPath, Event event, uint32_t requestId)>;
using OperGetItemsCb = std::function<ErrorCode(Session session, uint32_t subscriptionId, std::string_view moduleName, std::optional<std::string_view> subXPath, std::optional<std::string_view> requestXPath, uint32_t requestId, std::optional<libyang::DataNode>& output)>;
using RpcActionCb = std::function<ErrorCode(Session session, uint32_t subscriptionId, std::string_view path, const libyang::DataNode input, Event event, uint32_t requestId, libyang::DataNode output)>;
using NotifCb = std::function<void(Session session, uint32_t subscriptionId, const NotificationType type, const std::optional<libyang::DataNode> notificationTree, const NotificationTimeStamp timestamp)>;

class Subscription {
public:
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&&) noexcept;
    Subscription& operator=(Subscription&&) noexcept;

    void onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath = nullptr, uint32_t priority = 0, const SubscribeOptions opts = SubscribeOptions::Default);
    void onOperGetItems(const char* moduleName, OperGetItemsCb cb, const char* xpath = nullptr, const SubscribeOptions opts = SubscribeOptions::Default);
    void onRPCAction(const char* xpath, RpcActionCb cb, uint32_t priority = 0, const SubscribeOptions opts = SubscribeOptions::Default);
    void onNotification(
            const char* moduleName,
            NotifCb cb,
            const char* xpath = nullptr,
            const std::optional<NotificationTimeStamp>& startTime = std::nullopt,
            const std::optional<NotificationTimeStamp>& stopTime = std::nullopt,
            const SubscribeOptions opts = SubscribeOptions::Default);
private:
    void saveContext(sr_subscription_ctx_s* ctx);

    friend Session;
    explicit Subscription(std::shared_ptr<sr_session_ctx_s> sess);
    // This saves the users' callbacks. The C-style callback takes addresses of these, so the addresses need to be
    // stable (therefore, we use an std::list).
    std::list<ModuleChangeCb> m_moduleChangeCbs;
    std::list<OperGetItemsCb> m_operGetItemsCbs;
    std::list<RpcActionCb> m_RPCActionCbs;
    std::list<NotifCb> m_notificationCbs;

    std::shared_ptr<sr_session_ctx_s> m_sess;

    std::shared_ptr<sr_subscription_ctx_s> m_sub;
};
}
