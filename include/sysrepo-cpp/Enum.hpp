/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

namespace sysrepo {
/**
 * Wraps sr_error_t.
 */
enum class ErrorCode {
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
}
