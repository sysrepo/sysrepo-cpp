/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <sysrepo-cpp/Subscription.hpp>
#include <sysrepo-cpp/utils/utils.hpp>
extern "C" {
#include <sysrepo.h>
}
#include "utils/enum.hpp"
#include "utils/exception.hpp"
#include "utils/utils.hpp"

namespace sysrepo {
/**
 * Creates a Subscription instance with no actual underlying subscription associated with it. Internal use only.
 */
Subscription::Subscription(std::shared_ptr<sr_session_ctx_s> sess, ExceptionHandler handler, const std::optional<FDHandling>& callbacks)
    : m_customEventLoopCbs(callbacks)
    , m_exceptionHandler(std::make_unique<ExceptionHandler>(handler))
    , m_sess(sess)
{
}


int Subscription::eventPipe() const
{
    int pipe;
    auto res = sr_get_event_pipe(m_sub.get(), &pipe);
    throwIfError(res, "Couldn't retrieve event pipe");

    return pipe;
}

/**
 * Removes all subscriptions handled by this instance.
 */
Subscription::~Subscription()
{
    if (m_sub && m_customEventLoopCbs) {
        m_customEventLoopCbs->unregisterFd(eventPipe());
    }
}

/**
 * Saves the context on the first subscription created and also calls the custom event loop register function. Internal
 * use only.
 */
void Subscription::saveContext(sr_subscription_ctx_s* ctx)
{
    if (!m_sub) {
        m_sub = std::shared_ptr<sr_subscription_ctx_s>(ctx, sr_unsubscribe);
        if (m_customEventLoopCbs) {
            m_customEventLoopCbs->registerFd(eventPipe(), [sub = m_sub] {
                auto res = sr_subscription_process_events(sub.get(), nullptr, nullptr);
                throwIfError(res, "Couldn't process events");
            });
        }
    }
}

namespace {
void handleExceptionFromCb(std::exception& ex, std::function<void(std::exception& ex)>* exceptionHandler)
{
    if (!*exceptionHandler) {
        SRPLG_LOG_ERR("sysrepo-cpp", "User callback threw an exception: %s", ex.what());
        std::terminate();
    }

    try {
        (*exceptionHandler)(ex);
    } catch (std::exception& exFromHandler) {
        SRPLG_LOG_WRN("sysrepo-cpp", "Exception handler threw an exception: %s", exFromHandler.what());
        std::terminate();
    }
}

int moduleChangeCb(sr_session_ctx_t* session, uint32_t subscriptionId, const char* moduleName, const char* subXPath, sr_event_t event, uint32_t requestId, void* privateData)
{
    auto priv = reinterpret_cast<PrivData<ModuleChangeCb>*>(privateData);
    sysrepo::ErrorCode ret;
    try {
        ret = priv->callback(
                wrapUnmanagedSession(session),
                subscriptionId,
                moduleName,
                subXPath ? std::optional<std::string_view>{subXPath} : std::nullopt,
                toEvent(event),
                requestId);
    } catch (std::exception& ex) {
        ret = ErrorCode::OperationFailed;
        handleExceptionFromCb(ex, priv->exceptionHandler);
    }

    return static_cast<int>(ret);
}

int operGetItemsCb(sr_session_ctx_t* session, uint32_t subscriptionId, const char* moduleName, const char* subXPath, const char* requestXPath, uint32_t requestId, lyd_node** parent, void* privateData)
{
    auto priv = reinterpret_cast<PrivData<OperGetCb>*>(privateData);
    auto node = *parent ? std::optional{libyang::wrapRawNode(*parent)} : std::nullopt;
    sysrepo::ErrorCode ret;
    try {
        ret = priv->callback(
                    wrapUnmanagedSession(session),
                    subscriptionId,
                    moduleName,
                    subXPath ? std::optional{subXPath} : std::nullopt,
                    requestXPath ? std::optional{requestXPath} : std::nullopt,
                    requestId,
                    node);
    } catch (std::exception& ex) {
        ret = ErrorCode::OperationFailed;
        handleExceptionFromCb(ex, priv->exceptionHandler);
    }

    // The user can return no data or some data, which means std::nullopt or DataNode. We will map this to nullptr or a
    // lyd_node*.
    if (!node) {
        *parent = nullptr;
    } else {
        *parent = libyang::releaseRawNode(*node);
    }

    return static_cast<int>(ret);
}

int rpcActionCb(sr_session_ctx_t* session, uint32_t subscriptionId, const char* operationPath, const struct lyd_node* input, sr_event_t event, uint32_t requestId, struct lyd_node* output, void* privateData)
{
    auto priv = reinterpret_cast<PrivData<RpcActionCb>*>(privateData);
    auto outputNode = libyang::wrapRawNode(output);
    sysrepo::ErrorCode ret;
    try {
        ret = priv->callback(wrapUnmanagedSession(session),
                        subscriptionId,
                        operationPath,
                        libyang::wrapUnmanagedRawNode(input),
                        toEvent(event),
                        requestId,
                        outputNode
                );

    } catch (std::exception& ex) {
        ret = ErrorCode::OperationFailed;
        handleExceptionFromCb(ex, priv->exceptionHandler);
    }

    output = libyang::releaseRawNode(outputNode);

    return static_cast<int>(ret);
}

void eventNotifCb(sr_session_ctx_t* session, uint32_t subscriptionId, const sr_ev_notif_type_t type, const struct lyd_node* notification, struct timespec* timestamp, void *privateData)
{
    auto priv = reinterpret_cast<PrivData<NotifCb>*>(privateData);
    auto wrappedNotification = notification ? std::optional{libyang::wrapUnmanagedRawNode(notification)} : std::nullopt;
    try {
        priv->callback(wrapUnmanagedSession(session),
                        subscriptionId,
                        toNotificationType(type),
                        wrappedNotification,
                        toTimePoint(*timestamp)
                );

    } catch (std::exception& ex) {
        handleExceptionFromCb(ex, priv->exceptionHandler);
    }
}
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
 */
void Subscription::onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath, uint32_t priority, const SubscribeOptions opts)
{
    checkNoThreadFlag(opts, m_customEventLoopCbs);

    auto& privRef = m_moduleChangeCbs.emplace_back(PrivData{cb, m_exceptionHandler.get()});
    sr_subscription_ctx_s* ctx = m_sub.get();

    auto res = sr_module_change_subscribe(m_sess.get(), moduleName, xpath, moduleChangeCb, reinterpret_cast<void*>(&privRef), priority, toSubscribeOptions(opts), &ctx);
    throwIfError(res, "Couldn't create module change subscription");

    saveContext(ctx);
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
 */
void Subscription::onOperGet(const char* moduleName, OperGetCb cb, const char* xpath, const SubscribeOptions opts)
{
    checkNoThreadFlag(opts, m_customEventLoopCbs);

    auto& privRef = m_operGetCbs.emplace_back(PrivData{cb, m_exceptionHandler.get()});
    sr_subscription_ctx_s* ctx = m_sub.get();
    auto res = sr_oper_get_subscribe(m_sess.get(), moduleName, xpath, operGetItemsCb, reinterpret_cast<void*>(&privRef), toSubscribeOptions(opts), &ctx);
    throwIfError(res, "Couldn't create operational get items subscription");

    saveContext(ctx);
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
 */
void Subscription::onRPCAction(const char* xpath, RpcActionCb cb, uint32_t priority, const SubscribeOptions opts)
{
    checkNoThreadFlag(opts, m_customEventLoopCbs);

    auto& privRef = m_RPCActionCbs.emplace_back(PrivData{cb, m_exceptionHandler.get()});
    sr_subscription_ctx_s* ctx = m_sub.get();
    auto res = sr_rpc_subscribe_tree(m_sess.get(), xpath, rpcActionCb, reinterpret_cast<void*>(&privRef), priority, toSubscribeOptions(opts), &ctx);
    throwIfError(res, "Couldn't create RPC/action subscription");

    saveContext(ctx);
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
 */
void Subscription::onNotification(
        const char* moduleName,
        NotifCb cb,
        const char* xpath,
        const std::optional<NotificationTimeStamp>& startTime,
        const std::optional<NotificationTimeStamp>& stopTime,
        const SubscribeOptions opts)
{
    checkNoThreadFlag(opts, m_customEventLoopCbs);

    auto& privRef = m_notificationCbs.emplace_back(PrivData{cb, m_exceptionHandler.get()});
    sr_subscription_ctx_s* ctx = m_sub.get();
    auto startSpec = startTime ? std::optional{toTimespec(*startTime)} : std::nullopt;
    auto stopSpec = stopTime ? std::optional{toTimespec(*stopTime)} : std::nullopt;
    auto res = sr_notif_subscribe_tree(
            m_sess.get(),
            moduleName,
            xpath,
            startSpec ? &startSpec.value() : nullptr,
            stopSpec ? &stopSpec.value() : nullptr,
            eventNotifCb,
            reinterpret_cast<void*>(&privRef),
            toSubscribeOptions(opts),
            &ctx);
    throwIfError(res, "Couldn't create notification subscription");

    saveContext(ctx);
}

Subscription::Subscription(Subscription&& other) noexcept = default;

Subscription& Subscription::operator=(Subscription&& other) noexcept = default;

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

/**
 * Wraps `sr_change_iter_s`.
 */
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

/**
 * Advances this ChangeIterator.
 */
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

/**
 * Advances this ChangeIterator.
 */
ChangeIterator ChangeIterator::operator++(int)
{
    auto copy = *this;

    operator++();

    return copy;
}

/**
 * Retrieves the current change the iterator points to.
 */
const Change& ChangeIterator::operator*() const
{
    if (!m_current) {
        throw std::out_of_range("Dereferenced an .end iterator");
    }
    return *m_current;
}

/**
 * Retrieves the current change the iterator points to.
 */
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
