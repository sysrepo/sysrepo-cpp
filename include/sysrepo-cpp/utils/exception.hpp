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
 * @brief A generic sysrepo error. All other sysrepo errors inherit from this exception type.
 */
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief A sysrepo error containing a message and an error code.
 */
class ErrorWithCode : public Error {
public:
    /**
     * Creates a new sysrepo exception with the supplied message and code.
     */
    explicit ErrorWithCode(const std::string& what, uint32_t errCode);

    /**
     * Returns the error code.
     */
    ErrorCode code() const;
private:
    ErrorCode m_errCode;
};
}
