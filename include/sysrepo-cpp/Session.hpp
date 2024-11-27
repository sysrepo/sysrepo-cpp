/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <chrono>
#include <iosfwd>
#include <memory>
#include <optional>
#include <libyang-cpp/Context.hpp>
#include <libyang-cpp/DataNode.hpp>
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo-cpp/Subscription.hpp>

struct sr_conn_ctx_s;
struct sr_session_ctx_s;

namespace sysrepo {
class Connection;
class ChangeCollection;
class Session;

/**
 * @brief Internal use only.
 */
struct unmanaged_tag {
};

/**
 * @brief Contains info about a generic sysrepo error.
 */
struct ErrorInfo {
    bool operator==(const ErrorInfo& other) const = default;
    /**
     * The error code associated with the error.
     */
    ErrorCode code;
    /**
     * The error message.
     */
    std::string errorMessage;
};
std::ostream& operator<<(std::ostream& stream, const ErrorInfo& e);

/**
 * @brief Contains info about a NETCONF-style error.
 *
 * The meaning of the fields corresponds to the definition of NETCONF
 * [rpc-error](https://tools.ietf.org/html/rfc6241#section-4.3).
 */
struct NetconfErrorInfo {
    bool operator==(const NetconfErrorInfo& other) const = default;
    struct InfoElement {
        bool operator==(const InfoElement& other) const = default;
        std::string element;
        std::string value;
    };
    std::string type;
    std::string tag;
    std::optional<std::string> appTag;
    std::optional<std::string> path;
    std::string message;
    std::vector<InfoElement> infoElements;
};
std::ostream& operator<<(std::ostream& stream, const NetconfErrorInfo& e);

enum class Wait {
    Yes,
    No
};

sr_session_ctx_s* getRawSession(Session sess);

/**
 * @brief Handles a sysrepo session.
 */
class Session {
public:
    Datastore activeDatastore() const;
    void switchDatastore(const Datastore ds) const;
    void setItem(const std::string& path, const std::optional<std::string>& value, const EditOptions opts = sysrepo::EditOptions::Default);
    void editBatch(libyang::DataNode edit, const DefaultOperation op);
    void deleteItem(const std::string& path, const EditOptions opts = sysrepo::EditOptions::Default);
    void moveItem(const std::string& path, const MovePosition move, const std::optional<std::string>& keys_or_value, const std::optional<std::string>& origin = std::nullopt, const EditOptions opts = sysrepo::EditOptions::Default);
    std::optional<libyang::DataNode> getData(const std::string& path, int maxDepth = 0, const GetOptions opts = sysrepo::GetOptions::Default, std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) const;
    libyang::DataNode getOneNode(const std::string& path, std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) const;
    std::optional<const libyang::DataNode> getPendingChanges() const;
    void applyChanges(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    void discardChanges(const std::optional<std::string>& xpath = std::nullopt);
    std::optional<libyang::DataNode> operationalChanges(const std::optional<std::string>& moduleName = std::nullopt) const;
    void discardOperationalChanges(const std::optional<std::string>& moduleName = std::nullopt, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    void dropForeignOperationalContent(const std::optional<std::string>& xpath);
    void copyConfig(const Datastore source, const std::optional<std::string>& moduleName = std::nullopt, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    std::optional<libyang::DataNode> sendRPC(libyang::DataNode input, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    void sendNotification(libyang::DataNode notification, const Wait wait, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    void replaceConfig(std::optional<libyang::DataNode> config, const std::optional<std::string>& moduleName = std::nullopt, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

    void setNacmUser(const std::string& user);
    [[nodiscard]] Subscription initNacm(
            SubscribeOptions opts = SubscribeOptions::Default,
            ExceptionHandler handler = nullptr,
            const std::optional<FDHandling>& callbacks = std::nullopt);

    [[nodiscard]] Subscription onModuleChange(
            const std::string& moduleName,
            ModuleChangeCb cb,
            const std::optional<std::string>& xpath = std::nullopt,
            uint32_t priority = 0,
            const SubscribeOptions opts = SubscribeOptions::Default,
            ExceptionHandler handler = nullptr,
            const std::optional<FDHandling>& callbacks = std::nullopt);
    [[nodiscard]] Subscription onOperGet(
            const std::string& moduleName,
            OperGetCb cb,
            const std::optional<std::string>& xpath = std::nullopt,
            const SubscribeOptions opts = SubscribeOptions::Default,
            ExceptionHandler handler = nullptr,
            const std::optional<FDHandling>& callbacks = std::nullopt);
    [[nodiscard]] Subscription onRPCAction(const std::string& xpath,
            RpcActionCb cb,
            uint32_t priority = 0,
            const SubscribeOptions opts = SubscribeOptions::Default,
            ExceptionHandler handler = nullptr,
            const std::optional<FDHandling>& callbacks = std::nullopt);
    [[nodiscard]] Subscription onNotification(
            const std::string& moduleName,
            NotifCb cb,
            const std::optional<std::string>& xpath = std::nullopt,
            const std::optional<NotificationTimeStamp>& startTime = std::nullopt,
            const std::optional<NotificationTimeStamp>& stopTime = std::nullopt,
            const SubscribeOptions opts = SubscribeOptions::Default,
            ExceptionHandler handler = nullptr,
            const std::optional<FDHandling>& callbacks = std::nullopt);

    [[nodiscard]] DynamicSubscription yangPushPeriodic(
        const std::optional<std::string>& xpathFilter,
        std::chrono::milliseconds periodTime,
        const std::optional<NotificationTimeStamp>& anchorTime = std::nullopt,
        const std::optional<NotificationTimeStamp>& stopTime = std::nullopt);
    [[nodiscard]] DynamicSubscription yangPushOnChange(
        const std::optional<std::string>& xpathFilter,
        const std::optional<std::chrono::milliseconds>& dampeningPeriod = std::nullopt,
        SyncOnStart syncOnStart = SyncOnStart::No,
        const std::optional<NotificationTimeStamp>& stopTime = std::nullopt);
    [[nodiscard]] DynamicSubscription subscribeNotifications(
        const std::optional<std::string>& xpathFilter,
        const std::optional<std::string>& stream = std::nullopt,
        const std::optional<NotificationTimeStamp>& stopTime = std::nullopt,
        const std::optional<NotificationTimeStamp>& startTime = std::nullopt);

    ChangeCollection getChanges(const std::string& xpath = "//.");
    void setErrorMessage(const std::string& msg);
    void setNetconfError(const NetconfErrorInfo& info);

    std::vector<ErrorInfo> getErrors() const;
    std::vector<NetconfErrorInfo> getNetconfErrors() const;

    std::string getOriginatorName() const;
    void setOriginatorName(const std::string& originatorName);

    Connection getConnection();
    const libyang::Context getContext() const;

    uint32_t getId() const;

private:
    friend Connection;
    friend Session wrapUnmanagedSession(sr_session_ctx_s* session);
    friend sr_session_ctx_s* getRawSession(Session sess);

    Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn);
    explicit Session(sr_session_ctx_s* unmanagedSession, const unmanaged_tag);

    std::shared_ptr<sr_conn_ctx_s> m_conn;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};

/**
 * @brief Lock the current datastore, or a specified module in a datastore
 *
 * Lock release is controlled through [RAII](https://en.cppreference.com/w/cpp/language/raii).
 */
class Lock {
public:
    /** @brief Obtain a lock
     *
     * Wraps `sr_lock`.
     */
    explicit Lock(Session session, std::optional<std::string> moduleName = std::nullopt, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    /** @brief Release the lock
     *
     * Wraps `sr_unlock`.
     */
    ~Lock();

    Lock& operator=(const Lock& other) = delete;
    Lock(const Lock& other) = delete;
private:
    Session m_session;
    Datastore m_lockedDs;
    std::optional<std::string> m_module;
};
}
