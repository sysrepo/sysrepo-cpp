/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <doctest/doctest.h>
#include <experimental/iterator>
#include <sstream>
#include <sysrepo-cpp/Session.hpp>
#include <vector>

using namespace std::string_literals;

namespace std {
doctest::String toString(const std::vector<int32_t>& vec)
{
    std::ostringstream oss;
    oss << "std::vector<std::string>{\n    ";
    std::copy(vec.begin(), vec.end(), std::experimental::make_ostream_joiner(oss, ",\n    "));

    oss << "\n}";

    return oss.str().c_str();
}
}

namespace sysrepo {
doctest::String toString(const NetconfErrorInfo& info)
{
    std::ostringstream oss;
    oss << "sysrepo::NetconfErrorInfo {\n";
    oss << "    type: " << info.type << "\n";
    oss << "    tag: " << info.tag << "\n";
    oss << "    appTag: " << (info.appTag ? *info.appTag : "(none)") << "\n";
    oss << "    path: " << (info.path ? *info.path : "(none)") << "\n";
    oss << "    message: " << info.message << "\n";
    oss << "    infoElements: ";
    std::transform(info.infoElements.begin(), info.infoElements.end(), std::experimental::make_ostream_joiner(oss, ", "), [] (const auto& infoElem) {
        return "{" + infoElem.element + ", " + infoElem.value + "}";
    });
    oss << "\n}";

    return oss.str().c_str();
}
}
