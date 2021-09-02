/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#pragma once
#include <stdexcept>
#include <sysrepo-cpp/Enum.hpp>

namespace sysrepo {
/**
 * A generic sysrepo error. All other sysrepo errors inherit from this exception type.
 */
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * A sysrepo error containing a message and an error code.
 */
class ErrorWithCode : public Error {
public:
    explicit ErrorWithCode(const std::string& what, uint32_t errCode);

    ErrorCode code();
private:
    ErrorCode m_errCode;
};
}
