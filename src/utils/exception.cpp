/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#include <sstream>
#include <sysrepo.h>
#include <sysrepo-cpp/utils/exception.hpp>

namespace sysrepo {
ErrorWithCode::ErrorWithCode(const std::string& what, uint32_t errCode)
    : Error(what)
    , m_errCode(static_cast<ErrorCode>(errCode))
{
}

ErrorCode ErrorWithCode::code()
{
    return m_errCode;
}

// TODO: Idea for improvement: (maybe) use std::source_location when Clang supports it
void throwIfError(int code, std::string msg)
{
    if (code != SR_ERR_OK) {
        std::ostringstream oss;
        oss << msg << ": " << static_cast<ErrorCode>(code) << " (" << code << ")";
        throw ErrorWithCode(oss.str(), code);
    }
}
}
