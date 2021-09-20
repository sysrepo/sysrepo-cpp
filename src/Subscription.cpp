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
}

void Subscription::onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath, uint32_t priority, const SubscribeOptions opts)
{
    auto& cbRef = m_moduleChangeCbs.emplace_back(cb);
    sr_subscription_ctx_s* ctx = m_sub.get();

    auto res = sr_module_change_subscribe(m_sess.get(), moduleName, xpath, moduleChangeCb, reinterpret_cast<void*>(&cbRef), priority, toSubOptions(opts), &ctx);
    throwIfError(res, "Couldn't create module change subscription");

    saveContext(ctx);
}

Subscription::Subscription(Subscription&& other) noexcept
    : m_sess(other.m_sess)
    , m_sub(other.m_sub)
    , m_moduleChangeCbs(std::move(other.m_moduleChangeCbs))
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
}
