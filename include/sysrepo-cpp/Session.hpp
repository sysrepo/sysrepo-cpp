/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <libyang-cpp/Context.hpp>
#include <libyang-cpp/DataNode.hpp>
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo-cpp/Subscription.hpp>

struct sr_conn_ctx_s;
struct sr_data_s;
struct sr_session_ctx_s;
struct sr_val_s;

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
    void setItem(const char* path, const char* value, const EditOptions opts = sysrepo::EditOptions::Default);
    void editBatch(libyang::DataNode edit, const DefaultOperation op);
    void deleteItem(const char* path, const EditOptions opts = sysrepo::EditOptions::Default);
    void moveItem(const char* path, const MovePosition move, const char* keys_or_value, const char* origin = nullptr, const EditOptions opts = sysrepo::EditOptions::Default);
    // TODO: allow all arguments
    std::optional<libyang::DataNode> getData(const char* path) const;
    void applyChanges(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    void discardChanges();
    void copyConfig(const Datastore source, const char* moduleName = nullptr, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    libyang::DataNode sendRPC(libyang::DataNode input, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    void sendNotification(libyang::DataNode notification, const Wait wait, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

    void setNacmUser(const char* user);

    [[nodiscard]] Subscription onModuleChange(
            const char* moduleName,
            ModuleChangeCb cb,
            const char* xpath = nullptr,
            uint32_t priority = 0,
            const SubscribeOptions opts = SubscribeOptions::Default,
            ExceptionHandler handler = nullptr,
            const std::optional<FDHandling>& callbacks = std::nullopt);
    [[nodiscard]] Subscription onOperGet(
            const char* moduleName,
            OperGetCb cb,
            const char* xpath = nullptr,
            const SubscribeOptions opts = SubscribeOptions::Default,
            ExceptionHandler handler = nullptr,
            const std::optional<FDHandling>& callbacks = std::nullopt);
    [[nodiscard]] Subscription onRPCAction(const char* xpath,
            RpcActionCb cb,
            uint32_t priority = 0,
            const SubscribeOptions opts = SubscribeOptions::Default,
            ExceptionHandler handler = nullptr,
            const std::optional<FDHandling>& callbacks = std::nullopt);
    [[nodiscard]] Subscription onNotification(
            const char* moduleName,
            NotifCb cb,
            const char* xpath = nullptr,
            const std::optional<NotificationTimeStamp>& startTime = std::nullopt,
            const std::optional<NotificationTimeStamp>& stopTime = std::nullopt,
            const SubscribeOptions opts = SubscribeOptions::Default,
            ExceptionHandler handler = nullptr,
            const std::optional<FDHandling>& callbacks = std::nullopt);

    ChangeCollection getChanges(const char* xpath = "//.");
    void setErrorMessage(const char* msg);
    void setNetconfError(const NetconfErrorInfo& info);

    std::vector<ErrorInfo> getErrors() const;
    std::vector<NetconfErrorInfo> getNetconfErrors() const;

    std::string_view getOriginatorName() const;
    void setOriginatorName(const char* originatorName);

    Connection getConnection();
    const libyang::Context getContext() const;

private:
    friend Connection;
    friend Session wrapUnmanagedSession(sr_session_ctx_s* session);
    friend sr_session_ctx_s* getRawSession(Session sess);

    Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn);
    explicit Session(sr_session_ctx_s* unmanagedSession, const unmanaged_tag);

    std::shared_ptr<sr_conn_ctx_s> m_conn;
    std::shared_ptr<sr_session_ctx_s> m_sess;
};
}
