/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <cstdint>
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
    Operational
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
    NoThread = 2,
    Passive = 4,
    DoneOnly = 8,
    Enabled = 16,
    Update = 32,
    OperMerge = 64,
    ThreadSuspend = 128
};

/**
 * Wraps sr_edit_flag_t.
 */
enum class EditOptions : uint32_t {
    Default = 0,
    NonRecursive = 1,
    Strict = 2,
    Isolate = 4
};

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
    Created,
    Modified,
    Deleted,
    Moved,
};

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
}
