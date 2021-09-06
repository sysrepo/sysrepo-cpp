/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <memory>
#include <string_view>

struct sr_val_s;

namespace sysrepo {
class Session;

class Value {
public:
    std::string_view xpath() const;
private:
    friend Session;
    Value(sr_val_s*);

    std::unique_ptr<sr_val_s, void(*)(sr_val_s*)> m_val;
};
}
