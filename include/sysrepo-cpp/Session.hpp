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

struct unmanaged_tag {
};

struct ErrorInfo {
    ErrorCode code;
    std::optional<std::string> errorMessage;
};

enum class Wait {
    Yes,
    No
};

class Data {
public:
    libyang::DataNode tree();
    Connection connection();

    friend Session;

private:
    Data(sr_data_s* data);
    std::shared_ptr<sr_data_s> m_data;
    libyang::DataNode m_tree;
};

class Session {
public:
    Datastore activeDatastore() const;
    void switchDatastore(const Datastore ds) const;
    void setItem(const char* path, const char* value, const EditOptions opts = sysrepo::EditOptions::Default);
    void editBatch(libyang::DataNode edit, const DefaultOperation op);
    void deleteItem(const char* path, const EditOptions opts = sysrepo::EditOptions::Default);
    void moveItem(const char* path, const MovePosition move, const char* keys_or_value, const char* origin = nullptr, const EditOptions opts = sysrepo::EditOptions::Default);
    // TODO: allow all arguments
    std::optional<Data> getData(const char* path) const;
    void applyChanges(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    void discardChanges();
    void copyConfig(const Datastore source, const char* moduleName = nullptr, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    Data sendRPC(libyang::DataNode input, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
    void sendNotification(libyang::DataNode notification, const Wait wait, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

    [[nodiscard]] Subscription onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath = nullptr, uint32_t priority = 0, const SubscribeOptions opts = SubscribeOptions::Default, ExceptionHandler handler = nullptr);
    [[nodiscard]] Subscription onOperGetItems(const char* moduleName, OperGetItemsCb cb, const char* xpath = nullptr, const SubscribeOptions opts = SubscribeOptions::Default, ExceptionHandler handler = nullptr);
    [[nodiscard]] Subscription onRPCAction(const char* xpath, RpcActionCb cb, uint32_t priority = 0, const SubscribeOptions opts = SubscribeOptions::Default, ExceptionHandler handler = nullptr);
    [[nodiscard]] Subscription onNotification(
            const char* moduleName,
            NotifCb cb,
            const char* xpath = nullptr,
            const std::optional<NotificationTimeStamp>& startTime = std::nullopt,
            const std::optional<NotificationTimeStamp>& stopTime = std::nullopt,
            const SubscribeOptions opts = SubscribeOptions::Default,
            ExceptionHandler handler = nullptr);

    ChangeCollection getChanges(const char* xpath = "//.");
    void setErrorMessage(const char* msg);
    std::vector<ErrorInfo> getErrors();

    const libyang::Context getContext() const;

private:
    friend Connection;
    friend Session wrapUnmanagedSession(sr_session_ctx_s* session);

    Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn);
    explicit Session(sr_session_ctx_s* unmanagedSession, const unmanaged_tag);

    std::shared_ptr<sr_session_ctx_s> m_sess;
};
}
