/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#include <sstream>
#include <sysrepo.h>
#include <sysrepo-cpp/Session.hpp>
#include "exception.hpp"

namespace sysrepo {
ErrorWithCode::ErrorWithCode(const std::string& what, uint32_t errCode)
    : Error(what)
    , m_errCode(static_cast<ErrorCode>(errCode))
{
}

ErrorCode ErrorWithCode::code() const
{
    return m_errCode;
}

// TODO: Idea for improvement: (maybe) use std::source_location when Clang supports it
void throwIfError(int code, const std::string& msg, sr_session_ctx_s *c_session)
{
    if (code == SR_ERR_OK)
        return;

    std::ostringstream oss;
    oss << msg << ": " << static_cast<ErrorCode>(code);
    if (c_session) {
        for (const auto& err : impl_getErrors<ErrorInfo>(c_session)) {
            oss << "\n " << err;
        }
        for (const auto& err : impl_getErrors<NetconfErrorInfo>(c_session)) {
            oss << "\n NETCONF: " << err;
        }
    }
    throw ErrorWithCode(oss.str(), code);
}
}
