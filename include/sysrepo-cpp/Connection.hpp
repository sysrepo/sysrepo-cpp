/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/
#pragma once

#include <memory>

struct sr_conn_ctx_s;

namespace sysrepo {
class Connection {
public:
    Connection();
    ~Connection();
private:
    sr_conn_ctx_s *ctx;
};
}
