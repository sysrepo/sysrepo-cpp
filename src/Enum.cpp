/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#include <ostream>
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo.h>

namespace sysrepo {
std::ostream& operator<<(std::ostream& os, const NotificationType& type)
{
    switch (type) {
    case sysrepo::NotificationType::Realtime:
        return os << "sysrepo::NotificationType::Realtime";
    case sysrepo::NotificationType::Replay:
        return os << "sysrepo::NotificationType::Replay";
    case sysrepo::NotificationType::ReplayComplete:
        return os << "sysrepo::NotificationType::ReplayComplete";
    case sysrepo::NotificationType::Terminated:
        return os << "sysrepo::NotificationType::Terminated";
    case sysrepo::NotificationType::Modified:
        return os << "sysrepo::NotificationType::Modified";
    case sysrepo::NotificationType::Suspended:
        return os << "sysrepo::NotificationType::Suspended";
    case sysrepo::NotificationType::Resumed:
        return os << "sysrepo::NotificationType::Resumed";
    }

    return os << "[unknown event type]";
}

std::ostream& operator<<(std::ostream& os, const Event& event)
{
    switch (event) {
    case sysrepo::Event::Change:
        return os << "sysrepo::Event::Change";
    case sysrepo::Event::Done:
        return os << "sysrepo::Event::Done";
    case sysrepo::Event::Abort:
        return os << "sysrepo::Event::Abort";
    case sysrepo::Event::Enabled:
        return os << "sysrepo::Event::Enabled";
    case sysrepo::Event::RPC:
        return os << "sysrepo::Event::RPC";
    case sysrepo::Event::Update:
        return os << "sysrepo::Event::Update";
    }

    return os << "[unknown event type]";
}

std::ostream& operator<<(std::ostream& os, const ChangeOperation& changeOp)
{
    switch (changeOp) {
    case sysrepo::ChangeOperation::Created:
        return os << "sysrepo::ChangeOperation::Created";
    case sysrepo::ChangeOperation::Deleted:
        return os << "sysrepo::ChangeOperation::Deleted";
    case sysrepo::ChangeOperation::Modified:
        return os << "sysrepo::ChangeOperation::Modified";
    case sysrepo::ChangeOperation::Moved:
        return os << "sysrepo::ChangeOperation::Moved";
    }

    return os << "[unknown change operation type]";
}

#define CHECK_AND_STRINGIFY(CPP_ENUM, C_ENUM) \
    static_assert(static_cast<std::underlying_type_t<decltype(CPP_ENUM)>>(CPP_ENUM) == (C_ENUM)); \
    case CPP_ENUM: \
        return #C_ENUM

std::string stringify(const ErrorCode err)
{
    using enum sysrepo::ErrorCode;
    switch (err) {
    CHECK_AND_STRINGIFY(Ok, SR_ERR_OK);
    CHECK_AND_STRINGIFY(InvalidArgument, SR_ERR_INVAL_ARG);
    CHECK_AND_STRINGIFY(Libyang, SR_ERR_LY);
    CHECK_AND_STRINGIFY(SyscallFailed, SR_ERR_SYS);
    CHECK_AND_STRINGIFY(NotEnoughMemory, SR_ERR_NO_MEMORY);
    CHECK_AND_STRINGIFY(NotFound, SR_ERR_NOT_FOUND);
    CHECK_AND_STRINGIFY(ItemAlreadyExists, SR_ERR_EXISTS);
    CHECK_AND_STRINGIFY(Internal, SR_ERR_INTERNAL);
    CHECK_AND_STRINGIFY(Unsupported, SR_ERR_UNSUPPORTED);
    CHECK_AND_STRINGIFY(ValidationFailed, SR_ERR_VALIDATION_FAILED);
    CHECK_AND_STRINGIFY(OperationFailed, SR_ERR_OPERATION_FAILED);
    CHECK_AND_STRINGIFY(Unauthorized, SR_ERR_UNAUTHORIZED);
    CHECK_AND_STRINGIFY(Locked, SR_ERR_LOCKED);
    CHECK_AND_STRINGIFY(Timeout, SR_ERR_TIME_OUT);
    CHECK_AND_STRINGIFY(CallbackFailed, SR_ERR_CALLBACK_FAILED);
    CHECK_AND_STRINGIFY(CallbackShelve, SR_ERR_CALLBACK_SHELVE);
    }

    return "[unknown error code (" + std::to_string(static_cast<std::underlying_type_t<ErrorCode>>(err)) + ")]";
}

std::ostream& operator<<(std::ostream& os, const ErrorCode& err)
{
    os << stringify(err);
    return os;
}
}
