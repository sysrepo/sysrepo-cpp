/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <type_traits>
extern "C" {
#include <sysrepo.h>
}
#include <sysrepo-cpp/Enum.hpp>

namespace sysrepo {
static_assert(std::is_same_v<std::underlying_type_t<sr_datastore_t>, std::underlying_type_t<Datastore>>);

constexpr sr_datastore_t toDatastore(const Datastore options)
{
    return static_cast<sr_datastore_t>(options);
}

static_assert(toDatastore(Datastore::Running) == SR_DS_RUNNING);
static_assert(toDatastore(Datastore::Candidate) == SR_DS_CANDIDATE);
static_assert(toDatastore(Datastore::Operational) == SR_DS_OPERATIONAL);
static_assert(toDatastore(Datastore::Startup) == SR_DS_STARTUP);

constexpr Event toEvent(const sr_event_t event)
{
    return static_cast<Event>(event);
}

static_assert(std::is_same_v<std::underlying_type_t<sr_event_t>, std::underlying_type_t<Event>>);
static_assert(toEvent(SR_EV_UPDATE) == Event::Update);
static_assert(toEvent(SR_EV_CHANGE) == Event::Change);
static_assert(toEvent(SR_EV_DONE) == Event::Done);
static_assert(toEvent(SR_EV_ABORT) == Event::Abort);
static_assert(toEvent(SR_EV_ENABLED) == Event::Enabled);
static_assert(toEvent(SR_EV_RPC) == Event::RPC);

constexpr uint32_t toSubscribeOptions(const SubscribeOptions opts)
{
    return static_cast<uint32_t>(opts);
}

constexpr uint32_t toEditOptions(const EditOptions opts)
{
    return static_cast<uint32_t>(opts);
}

template <typename Enum>
constexpr Enum implEnumBitOr(const Enum a, const Enum b)
{
    using Type = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<Type>(a) | static_cast<Type>(b));
}

constexpr EditOptions operator|(const EditOptions a, const EditOptions b)
{
    return implEnumBitOr(a, b);
}

static_assert(std::is_same_v<std::underlying_type_t<sr_edit_flag_t>, std::underlying_type_t<EditOptions>>);
static_assert(toEditOptions(EditOptions::Default) == SR_EDIT_DEFAULT);
static_assert(toEditOptions(EditOptions::NonRecursive) == SR_EDIT_NON_RECURSIVE);
static_assert(toEditOptions(EditOptions::Strict) == SR_EDIT_STRICT);
static_assert(toEditOptions(EditOptions::Isolate) == SR_EDIT_ISOLATE);

constexpr SubscribeOptions operator|(const SubscribeOptions a, const SubscribeOptions b)
{
    return implEnumBitOr(a, b);
}

static_assert(std::is_same_v<std::underlying_type_t<sr_subscr_flag_t>, std::underlying_type_t<SubscribeOptions>>);

static_assert(toSubscribeOptions(SubscribeOptions::Default) == SR_SUBSCR_DEFAULT);
static_assert(toSubscribeOptions(SubscribeOptions::NoThread) == SR_SUBSCR_NO_THREAD);
static_assert(toSubscribeOptions(SubscribeOptions::Passive) == SR_SUBSCR_PASSIVE);
static_assert(toSubscribeOptions(SubscribeOptions::DoneOnly) == SR_SUBSCR_DONE_ONLY);
static_assert(toSubscribeOptions(SubscribeOptions::Enabled) == SR_SUBSCR_ENABLED);
static_assert(toSubscribeOptions(SubscribeOptions::Update) == SR_SUBSCR_UPDATE);
static_assert(toSubscribeOptions(SubscribeOptions::OperMerge) == SR_SUBSCR_OPER_MERGE);
static_assert(toSubscribeOptions(SubscribeOptions::ThreadSuspend) == SR_SUBSCR_THREAD_SUSPEND);

constexpr ChangeOperation toChangeOper(const sr_change_oper_t oper)
{
    return static_cast<ChangeOperation>(oper);
}

static_assert(std::is_same_v<std::underlying_type_t<sr_change_oper_t>, std::underlying_type_t<ChangeOperation>>);

static_assert(toChangeOper(SR_OP_CREATED) == ChangeOperation::Created);
static_assert(toChangeOper(SR_OP_MODIFIED) == ChangeOperation::Modified);
static_assert(toChangeOper(SR_OP_DELETED) == ChangeOperation::Deleted);
static_assert(toChangeOper(SR_OP_MOVED) == ChangeOperation::Moved);

constexpr sr_log_level_t toLogLevel(const LogLevel level)
{
    return static_cast<sr_log_level_t>(level);
}

static_assert(std::is_same_v<std::underlying_type_t<sr_log_level_t>, std::underlying_type_t<LogLevel>>);

static_assert(toLogLevel(LogLevel::None) == SR_LL_NONE);
static_assert(toLogLevel(LogLevel::Error) == SR_LL_ERR);
static_assert(toLogLevel(LogLevel::Warning) == SR_LL_WRN);
static_assert(toLogLevel(LogLevel::Information) == SR_LL_INF);
static_assert(toLogLevel(LogLevel::Debug) == SR_LL_DBG);

static_assert(std::is_same_v<std::underlying_type_t<sr_error_t>, std::underlying_type_t<ErrorCode>>);

static_assert(static_cast<ErrorCode>(SR_ERR_OK) == ErrorCode::Ok);
static_assert(static_cast<ErrorCode>(SR_ERR_INVAL_ARG) == ErrorCode::InvalidArgument);
static_assert(static_cast<ErrorCode>(SR_ERR_LY) == ErrorCode::Libyang);
static_assert(static_cast<ErrorCode>(SR_ERR_SYS) == ErrorCode::SyscallFailed);
static_assert(static_cast<ErrorCode>(SR_ERR_NO_MEMORY) == ErrorCode::NotEnoughMemory);
static_assert(static_cast<ErrorCode>(SR_ERR_NOT_FOUND) == ErrorCode::NotFound);
static_assert(static_cast<ErrorCode>(SR_ERR_EXISTS) == ErrorCode::ItemAlreadyExists);
static_assert(static_cast<ErrorCode>(SR_ERR_INTERNAL) == ErrorCode::Internal);
static_assert(static_cast<ErrorCode>(SR_ERR_UNSUPPORTED) == ErrorCode::Unsupported);
static_assert(static_cast<ErrorCode>(SR_ERR_VALIDATION_FAILED) == ErrorCode::ValidationFailed);
static_assert(static_cast<ErrorCode>(SR_ERR_OPERATION_FAILED) == ErrorCode::OperationFailed);
static_assert(static_cast<ErrorCode>(SR_ERR_UNAUTHORIZED) == ErrorCode::Unauthorized);
static_assert(static_cast<ErrorCode>(SR_ERR_LOCKED) == ErrorCode::Locked);
static_assert(static_cast<ErrorCode>(SR_ERR_TIME_OUT) == ErrorCode::Timeout);
static_assert(static_cast<ErrorCode>(SR_ERR_CALLBACK_FAILED) == ErrorCode::CallbackFailed);
static_assert(static_cast<ErrorCode>(SR_ERR_CALLBACK_SHELVE) == ErrorCode::CallbackShelve);

constexpr const char* toDefaultOperation(const DefaultOperation op) {
    switch (op) {
    case DefaultOperation::Merge:
        return "merge";
    case DefaultOperation::Replace:
        return "replace";
    case DefaultOperation::None:
        return "none";
    }

    __builtin_unreachable();
}
}
