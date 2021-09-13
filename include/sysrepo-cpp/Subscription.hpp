/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once
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
    struct ChangeIteratorEnd{
    };

    ChangeIterator& operator++();
    const Change& operator*() const;
    const Change& operator->() const;
    bool operator==(const ChangeIterator::ChangeIteratorEnd) const;

private:
    ChangeIterator(sr_change_iter_s* iter, std::shared_ptr<sr_session_ctx_s> sess);
    friend ChangeCollection;

    std::optional<Change> m_current;

    std::shared_ptr<sr_change_iter_s> m_iter;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};

class ChangeCollection {
public:
    ChangeIterator begin() const;
    ChangeIterator::ChangeIteratorEnd end() const;

private:
    ChangeCollection(const char* xpath, std::shared_ptr<sr_session_ctx_s> sess);
    friend Session;
    std::string m_xpath;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};

using ModuleChangeCb = std::function<ErrorCode(Session session, uint32_t subscriptionId, std::string_view moduleName, std::optional<std::string_view> subXPath, Event event, uint32_t requestId)>;

class Subscription {
public:
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&&) noexcept;
    Subscription& operator=(Subscription&&) noexcept;

    void onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath = nullptr, uint32_t priority = 0, const SubOptions opts = SubOptions::Default);
private:
    void saveContext(sr_subscription_ctx_s* ctx);

    friend Session;
    explicit Subscription(std::shared_ptr<sr_session_ctx_s> sess);
    std::shared_ptr<sr_subscription_ctx_s> m_sub;
    std::shared_ptr<sr_session_ctx_s> m_sess;

    // This saves the users' callbacks. The C-style callback takes addresses of these, so the addresses need to be
    // stable (therefore, we use an std::list).
    std::list<ModuleChangeCb> m_moduleChangeCbs;
};
}
