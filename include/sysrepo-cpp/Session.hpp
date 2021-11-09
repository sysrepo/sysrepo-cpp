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
struct sr_session_ctx_s;
struct sr_val_s;

namespace sysrepo {
class Connection;
class ChangeCollection;
class Session;

struct unmanaged_tag {
};

enum class Wait {
    Yes,
    No
};

class Session {
public:
    Datastore activeDatastore() const;
    void switchDatastore(const Datastore ds) const;
    // TODO: allow all arguments
    void setItem(const char* path, const char* value) const;
    void editBatch(libyang::DataNode edit, const DefaultOperation op) const;
    void deleteItem(const char* path, const EditOptions opts = sysrepo::EditOptions::Default) const;
    void moveItem(const char* path, const MovePosition move, const char* keys_or_value, const char* origin = nullptr, const EditOptions opts = sysrepo::EditOptions::Default) const;
    // TODO: allow all arguments
    std::optional<libyang::DataNode> getData(const char* path) const;
    void applyChanges(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) const;
    void discardChanges() const;
    void copyConfig(const Datastore source, const char* moduleName = nullptr, std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) const;

    libyang::DataNode sendRPC(libyang::DataNode input, std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) const;
    void sendNotification(libyang::DataNode notification, const Wait wait, std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) const;

    [[nodiscard]] Subscription onModuleChange(const char* moduleName, ModuleChangeCb cb, const char* xpath = nullptr, uint32_t priority = 0, const SubscribeOptions opts = SubscribeOptions::Default) const;
    [[nodiscard]] Subscription onOperGetItems(const char* moduleName, OperGetItemsCb cb, const char* xpath = nullptr, const SubscribeOptions opts = SubscribeOptions::Default) const;
    [[nodiscard]] Subscription onRPCAction(const char* xpath, RpcActionCb cb, uint32_t priority = 0, const SubscribeOptions opts = SubscribeOptions::Default) const;
    [[nodiscard]] Subscription onNotification(
            const char* moduleName,
            NotifCb cb,
            const char* xpath = nullptr,
            const std::optional<NotificationTimeStamp>& startTime = std::nullopt,
            const std::optional<NotificationTimeStamp>& stopTime = std::nullopt,
            const SubscribeOptions opts = SubscribeOptions::Default) const;

    ChangeCollection getChanges(const char* xpath = "//.") const;

    const libyang::Context getContext() const;

private:
    friend Connection;
    friend Session wrapUnmanagedSession(sr_session_ctx_s* session);

    Session(sr_session_ctx_s* sess, std::shared_ptr<sr_conn_ctx_s> conn);
    explicit Session(sr_session_ctx_s* unmanagedSession, const unmanaged_tag);

    std::shared_ptr<sr_session_ctx_s> m_sess;
};
}
