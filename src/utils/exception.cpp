/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
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
}
