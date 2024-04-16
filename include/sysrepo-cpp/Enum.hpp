/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <cstdint>
#include <iosfwd>
#include <type_traits>
namespace sysrepo {
/**
 * Wraps sr_error_t.
 */
enum class ErrorCode : uint32_t {
    Ok = 0,
    InvalidArgument,
    Libyang,
    SyscallFailed,
    NotEnoughMemory,
    NotFound,
    ItemAlreadyExists,
    Internal,
    Unsupported,
    ValidationFailed,
    OperationFailed,
    Unauthorized,
    Locked,
    Timeout,
    CallbackFailed,
    CallbackShelve
};

/**
 * Wraps sr_datastore_t.
 */
enum class Datastore : uint32_t {
    Startup,
    Running,
    Candidate,
    Operational,
    FactoryDefault,
};

/**
 * Wraps sr_event_t.
 */
enum class Event : uint32_t {
    Update,
    Change,
    Done,
    Abort,
    Enabled,
    RPC
};

/**
 * Wraps sr_subscr_flag_t.
 */
enum class SubscribeOptions : uint32_t {
    Default = 0,
    NoThread = 1,
    Passive = 2,
    DoneOnly = 4,
    Enabled = 8,
    Update = 16,
    OperMerge = 32,
    ThreadSuspend = 64
};

template <typename Enum>
constexpr Enum implEnumBitOr(const Enum a, const Enum b)
{
    using Type = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<Type>(a) | static_cast<Type>(b));
}

template <typename Enum>
constexpr bool implEnumBitAnd(const Enum a, const Enum b)
{
    using Type = std::underlying_type_t<Enum>;
    return static_cast<Type>(a) & static_cast<Type>(b);
}

constexpr SubscribeOptions operator|(const SubscribeOptions a, const SubscribeOptions b)
{
    return implEnumBitOr(a, b);
}

constexpr bool operator&(const SubscribeOptions a, const SubscribeOptions b)
{
    return implEnumBitAnd(a, b);
}

/**
 * Wraps sr_edit_flag_t.
 */
enum class EditOptions : uint32_t {
    Default = 0,
    NonRecursive = 1,
    Strict = 2,
    Isolate = 4
};

constexpr EditOptions operator|(const EditOptions a, const EditOptions b)
{
    return implEnumBitOr(a, b);
}

/**
 * Wraps sr_move_position_t.
 */
enum class MovePosition : uint32_t {
    Before,
    After,
    First,
    Last
};

/**
 * Wraps sr_change_oper_t.
 */
enum class ChangeOperation : uint32_t {
    /**
     * The item has been created by the change.
     */
    Created,
    /**
     * The value has been modified by the change.
     */
    Modified,
    /**
     * The item has been deleted by the change.
     */
    Deleted,
    /**
     * The item has been moved by the change. Only applies to user-ordered lists and leaf-lists.
     */
    Moved,
};

/**
 * The argument for `sysrepo::Session::editBatch`.
 * Note: The argument is a string in the C API.
 */
enum class DefaultOperation {
    Merge,
    Replace,
    None
};

/**
 * Wraps sr_log_level_t.
 */
enum class LogLevel : uint32_t {
    None,
    Error,
    Warning,
    Information,
    Debug
};

/**
 * Wraps sr_ev_notif_type_t.
 */
enum class NotificationType : uint32_t {
    Realtime,
    Replay,
    ReplayComplete,
    Terminated,
    Modified,
    Suspended,
    Resumed
};

/**
 * Wraps `sr_conn_flag_e`.
 */
enum class ConnectionFlags : uint32_t {
    Default = 0x00, /**< SR_CONN_DEFAULT */
    CacheRunning = 0x01, /**< SR_CONN_CACHE_RUNNING */
    LibYangPrivParsed = 0x02, /**< SR_CONN_CTX_SET_PRIV_PARSED */
};

constexpr ConnectionFlags operator|(const ConnectionFlags a, const ConnectionFlags b)
{
    return implEnumBitOr(a, b);
}

/**
 * Wraps `sr_get_options_t`.
 */
enum class GetOptions : uint32_t {
    OperDefault = 0x00, /**< SR_OPER_DEFAULT */
    OperNoState = 0x01, /**< SR_OPER_NO_STATE */
    OperNoConfig = 0x02, /**< SR_OPER_NO_CONFIG */
    OperNoSubs = 0x04, /**< SR_OPER_NO_SUBS */
    OperNoStored = 0x08, /**< SR_OPER_NO_STORED */
    OperWithOrigin = 0x10, /**< SR_OPER_WITH_ORIGIN */
    OperNoPollCached = 0x20, /**< SR_OPER_NO_POLL_CACHED */
    OperNoRunCached = 0x40, /**< SR_OPER_NO_RUN_CACHED */
    NoFilter = 0x010000, /**< SR_GET_NO_FILTER */
};

constexpr GetOptions operator|(const GetOptions a, const GetOptions b)
{
    return implEnumBitOr(a, b);
}

std::ostream& operator<<(std::ostream& os, const NotificationType& type);
std::ostream& operator<<(std::ostream& os, const Event& event);
std::ostream& operator<<(std::ostream& os, const ChangeOperation& changeOp);
std::ostream& operator<<(std::ostream& os, const ErrorCode& err);
}
