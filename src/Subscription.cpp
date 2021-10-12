/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <sysrepo-cpp/Subscription.hpp>
extern "C" {
#include <sysrepo.h>
}
#include "utils/enum.hpp"
#include "utils/exception.hpp"
#include "utils/utils.hpp"

namespace sysrepo {
Subscription::Subscription(std::shared_ptr<sr_session_ctx_s> sess)
    : m_sess(sess)
{
}

void Subscription::saveContext(sr_subscription_ctx_s* ctx)
{
    if (!m_sub) {
        m_sub = std::shared_ptr<sr_subscription_ctx_s>(ctx, sr_unsubscribe);
    }
}

namespace {
int moduleChangeCb(sr_session_ctx_t* session, uint32_t subscriptionId, const char* moduleName, const char* subXPath, sr_event_t event, uint32_t requestId, void* privateData)
{
    auto cb = reinterpret_cast<ModuleChangeCb*>(privateData);
    return static_cast<int>((*cb)(wrapUnmanagedSession(session), subscriptionId, moduleName, subXPath ? std::optional<std::string_view>{subXPath} : std::nullopt, toEvent(event), requestId));
}

int operGetItemsCb(sr_session_ctx_t* session, uint32_t subscriptionId, const char* moduleName, const char* subXPath, const char* requestXPath, uint32_t requestId, lyd_node** parent, void* privateData)
{
    auto cb = reinterpret_cast<OperGetItemsCb*>(privateData);
    auto node = *parent ? std::optional{libyang::wrapRawNode(*parent)} : std::nullopt;
    auto ret = ((*cb)(
                wrapUnmanagedSession(session),
                subscriptionId,
                moduleName,
                subXPath ? std::optional{subXPath} : std::nullopt,
                requestXPath ? std::optional{requestXPath} : std::nullopt,
                requestId,
                node));

    if (!node) {
        *parent = nullptr;
    } else {
        *parent = libyang::releaseRawNode(*node);
    }

    return static_cast<int>(ret);
}

int rpcActionCb(sr_session_ctx_t* session, uint32_t subscriptionId, const char* operationPath, const struct lyd_node* input, sr_event_t event, uint32_t requestId, struct lyd_node* output, void* privateData)
{
    auto cb = reinterpret_cast<RpcActionCb*>(privateData);
    auto outputNode = libyang::wrapRawNode(output);
    auto ret = (*cb)(wrapUnmanagedSession(session),
                    subscriptionId,
                    operationPath,
                    libyang::wrapUnmanagedRawNode(input),
                    toEvent(event),
                    requestId,
                    outputNode
            );
    output = libyang::releaseRawNode(outputNode);

    return static_cast<int>(ret);
}
}

void Subscription::onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath, uint32_t priority, const SubscribeOptions opts)
{
    auto& cbRef = m_moduleChangeCbs.emplace_back(cb);
    sr_subscription_ctx_s* ctx = m_sub.get();

    auto res = sr_module_change_subscribe(m_sess.get(), moduleName, xpath, moduleChangeCb, reinterpret_cast<void*>(&cbRef), priority, toSubscribeOptions(opts), &ctx);
    throwIfError(res, "Couldn't create module change subscription");

    saveContext(ctx);
}

void Subscription::onOperGetItems(const char* moduleName, OperGetItemsCb cb, const char* xpath, const SubscribeOptions opts)
{
    auto& cbRef = m_operGetItemsCbs.emplace_back(cb);
    sr_subscription_ctx_s* ctx = m_sub.get();
    auto res = sr_oper_get_items_subscribe(m_sess.get(), moduleName, xpath, operGetItemsCb, reinterpret_cast<void*>(&cbRef), toSubscribeOptions(opts), &ctx);
    throwIfError(res, "Couldn't create operational get items subscription");

    saveContext(ctx);
}

void Subscription::onRPCAction(const char* xpath, RpcActionCb cb, uint32_t priority, const SubscribeOptions opts)
{
    auto& cbRef = m_RPCActionCbs.emplace_back(cb);
    sr_subscription_ctx_s* ctx = m_sub.get();
    auto res = sr_rpc_subscribe_tree(m_sess.get(), xpath, rpcActionCb, reinterpret_cast<void*>(&cbRef), priority, toSubscribeOptions(opts), &ctx);
    throwIfError(res, "Couldn't create RPC/action subscription");

    saveContext(ctx);
}

Subscription::Subscription(Subscription&& other) noexcept
    : m_moduleChangeCbs(std::move(other.m_moduleChangeCbs))
    , m_sess(other.m_sess)
    , m_sub(other.m_sub)
{
}

Subscription& Subscription::operator=(Subscription&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    m_sub = other.m_sub;
    m_sess = other.m_sess;
    m_moduleChangeCbs = std::move(other.m_moduleChangeCbs);

    return *this;
}

ChangeCollection::ChangeCollection(const char* xpath, std::shared_ptr<sr_session_ctx_s> sess)
    : m_xpath(xpath)
    , m_sess(sess)
{
}

/**
 * Creates a `begin` iterator for the iterator.
 */
ChangeIterator ChangeCollection::begin() const
{
    sr_change_iter_t* iter;
    auto res = sr_get_changes_iter(m_sess.get(), m_xpath.c_str(), &iter);

    throwIfError(res, "Couldn't create an iterator for changes");

    return ChangeIterator{iter, m_sess};
}

/**
 * Creates an `end` iterator for the iterator.
 */
ChangeIterator ChangeCollection::end() const
{
    return ChangeIterator{ChangeIterator::iterator_end_tag{}};
}

ChangeIterator::ChangeIterator(sr_change_iter_s* iter, std::shared_ptr<sr_session_ctx_s> sess)
    : m_iter(iter, sr_free_change_iter)
    , m_sess(sess)
{
    operator++();
}

ChangeIterator::ChangeIterator(const iterator_end_tag)
    : m_current(std::nullopt)
    , m_iter(nullptr)
    , m_sess(nullptr)
{
}

ChangeIterator& ChangeIterator::operator++()
{
    sr_change_oper_t operation;
    const lyd_node* node;
    const char* prevValue;
    const char* prevList;
    int prevDefault;
    auto ret = sr_get_change_tree_next(m_sess.get(), m_iter.get(), &operation, &node, &prevValue, &prevList, &prevDefault);

    if (ret == SR_ERR_NOT_FOUND) {
        m_current = std::nullopt;
        return *this;
    }

    throwIfError(ret, "Could not iterate to the next change");

    // I can safely "dereference" the change here, because last change is handled by the condition above.
    m_current.emplace(Change{
            .operation = toChangeOper(operation),
            .node = libyang::wrapUnmanagedRawNode(node),
            .previousValue = prevValue ? std::optional<std::string_view>(prevValue) : std::nullopt,
            .previousList = prevList ? std::optional<std::string_view>(prevList) : std::nullopt,
            .previousDefault = static_cast<bool>(prevDefault),
    });

    return *this;
}

ChangeIterator ChangeIterator::operator++(int)
{
    auto copy = *this;

    operator++();

    return copy;
}

const Change& ChangeIterator::operator*() const
{
    if (!m_current) {
        throw std::out_of_range("Dereferenced an .end iterator");
    }
    return *m_current;
}

const Change& ChangeIterator::operator->() const
{
    if (!m_current) {
        throw std::out_of_range("Dereferenced an .end iterator");
    }
    return *m_current;
}

/**
 * Compares two iterators.
 */
bool ChangeIterator::operator==(const ChangeIterator& other) const
{
    // Both instances need to either contain a value or both contain nothing.
    return this->m_current.has_value() == other.m_current.has_value() &&
        // And then either both contain nothing or contain the same thing.
        (!this->m_current.has_value() || this->m_current->node == other.m_current->node);
}
}
