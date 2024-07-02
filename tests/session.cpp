/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 * SPDX-License-Identifier: BSD-3-Clause
*/

#include <doctest/doctest.h>
#include <optional>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/utils/utils.hpp>
#include <sysrepo-cpp/utils/exception.hpp>

using namespace std::literals;

TEST_CASE("session")
{
    sysrepo::setLogLevelStderr(sysrepo::LogLevel::Information);
    std::optional<sysrepo::Connection> conn{std::in_place};
    auto sess = conn->sessionStart();
    sess.copyConfig(sysrepo::Datastore::Startup);

    DOCTEST_SUBCASE("Session should be still valid even after the Connection class gets freed")
    {
        conn = std::nullopt;
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Running);
    }

    DOCTEST_SUBCASE("Session lifetime is prolonged with data from getData")
    {
        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();
        auto data = sysrepo::Connection{}.sessionStart().getData("/test_module:leafInt32");
        REQUIRE(data->asTerm().valueStr() == "123");
    }

    DOCTEST_SUBCASE("basic data manipulation")
    {
        auto data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);

        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(data->asTerm().valueStr() == "123");
        auto node = sess.getOneNode("/test_module:leafInt32");
        REQUIRE(node.asTerm().valueStr() == "123");

        sess.setItem("/test_module:leafInt32", "420");
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(data->asTerm().valueStr() == "420");

        sess.deleteItem("/test_module:leafInt32");
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);

        sess.setItem("/test_module:leafInt32", "123");
        sess.discardChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);
        REQUIRE_THROWS_WITH_AS(sess.getOneNode("/test_module:leafInt32"),
                "Session::getOneNode: Couldn't get '/test_module:leafInt32': SR_ERR_NOT_FOUND",
                sysrepo::ErrorWithCode);

        sess.setItem("/test_module:popelnice/s", "yay 42");
        data = sess.getData("/test_module:popelnice/s");
        REQUIRE(!!data);
        REQUIRE(data->path() == "/test_module:popelnice");
        auto x = data->findPath("/test_module:popelnice/s");
        REQUIRE(!!x);
        REQUIRE(x->asTerm().valueStr() == "yay 42");
        node = sess.getOneNode("/test_module:popelnice/s");
        REQUIRE(node.path() == "/test_module:s");
        REQUIRE(node.schema().path() == "/test_module:popelnice/s");
        REQUIRE(node.asTerm().valueStr() == "yay 42");
        node = sess.getOneNode("/test_module:popelnice");
        REQUIRE(node.path() == "/test_module:popelnice");
        REQUIRE(!node.isTerm());
        sess.discardChanges();

        REQUIRE_THROWS_WITH_AS(sess.setItem("/test_module:non-existent", std::nullopt),
                "Session::setItem: Couldn't set '/test_module:non-existent': SR_ERR_LY\n"
                " Not found node \"non-existent\" in path. (SR_ERR_LY)",
                sysrepo::ErrorWithCode);

        REQUIRE_THROWS_WITH_AS(sess.getData("/test_module:non-existent"),
                "Session::getData: Couldn't get '/test_module:non-existent': SR_ERR_NOT_FOUND",
                sysrepo::ErrorWithCode);

        REQUIRE_THROWS_WITH_AS(sess.getOneNode("/test_module:non-existent"),
                "Session::getOneNode: Couldn't get '/test_module:non-existent': SR_ERR_NOT_FOUND",
                sysrepo::ErrorWithCode);
    }

    DOCTEST_SUBCASE("Session::getData")
    {
        DOCTEST_SUBCASE("max depth")
        {
            sess.setItem("/test_module:popelnice/content/trash[name='c++']/cont/l", "hi");
            sess.setItem("/test_module:popelnice/content/trash[name='rust']", std::nullopt);

            auto data = sess.getData("/test_module:popelnice", 0);
            REQUIRE(data);
            REQUIRE(*data->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::KeepEmptyCont) == R"({
  "test_module:popelnice": {
    "content": {
      "trash": [
        {
          "name": "c++",
          "cont": {
            "l": "hi"
          }
        },
        {
          "name": "rust"
        }
      ]
    }
  }
}
)");

            data = sess.getData("/test_module:popelnice", 1);
            REQUIRE(data);
            REQUIRE(*data->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::KeepEmptyCont) == R"({
  "test_module:popelnice": {
    "content": {}
  }
}
)");

            // If a list should be returned, its keys are always returned as well.
            data = sess.getData("/test_module:popelnice", 2);
            REQUIRE(data);
            REQUIRE(*data->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::KeepEmptyCont) == R"({
  "test_module:popelnice": {
    "content": {
      "trash": [
        {
          "name": "c++"
        },
        {
          "name": "rust"
        }
      ]
    }
  }
}
)");

            data = sess.getData("/test_module:popelnice", 3);
            REQUIRE(data);
            REQUIRE(*data->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::KeepEmptyCont) == R"({
  "test_module:popelnice": {
    "content": {
      "trash": [
        {
          "name": "c++",
          "cont": {}
        },
        {
          "name": "rust"
        }
      ]
    }
  }
}
)");
        }

        DOCTEST_SUBCASE("options for operational DS")
        {
            sess.switchDatastore(sysrepo::Datastore::Operational);
            sess.setItem("/test_module:stateLeaf", "42");
            sess.setItem("/test_module:leafInt32", "1");
            sess.applyChanges();

            DOCTEST_SUBCASE("Default options")
            {
                auto data = sess.getData("/test_module:*");
                REQUIRE(data);
                REQUIRE(data->findPath("/test_module:stateLeaf"));
                REQUIRE(data->findPath("/test_module:leafInt32"));
            }

            DOCTEST_SUBCASE("No state data")
            {
                auto data = sess.getData("/test_module:*", 0, sysrepo::GetOptions::OperNoState);
                REQUIRE(data);
                REQUIRE(!data->findPath("/test_module:stateLeaf"));
                REQUIRE(data->findPath("/test_module:leafInt32"));
            }
        }

        DOCTEST_SUBCASE("options and running ds")
        {
            sess.switchDatastore(sysrepo::Datastore::Running);

            auto data = sess.getData("/test_module:*");
            REQUIRE(data);
            REQUIRE(data->findPath("/test_module:leafWithDefault"));

            REQUIRE_THROWS_WITH_AS(sess.getData("/test_module:*", 0, sysrepo::GetOptions::OperNoState | sysrepo::GetOptions::OperNoConfig | sysrepo::GetOptions::NoFilter),
                                   "Session::getData: Couldn't get '/test_module:*': SR_ERR_INVAL_ARG\n"
                                   " Invalid arguments for function \"sr_get_data\". (SR_ERR_INVAL_ARG)",
                                   sysrepo::ErrorWithCode);
        }
    }

    DOCTEST_SUBCASE("Session::deleteOperItem")
    {
        // Set some arbitrary leaf.
        sess.setItem("/test_module:leafInt32", "123");
        sess.applyChanges();

        // The leaf is accesible from the running datastore.
        REQUIRE(sess.getData("/test_module:leafInt32")->asTerm().valueStr() == "123");

        // The leaf is NOT accesible from the operational datastore without a subscription.
        sess.switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE(!sess.getData("/test_module:leafInt32"));

        // When we create a subscription, the leaf is accesible from the operational datastore.
        sess.switchDatastore(sysrepo::Datastore::Running);
        auto sub = sess.onModuleChange("test_module", [] (auto, auto, auto, auto, auto, auto) { return sysrepo::ErrorCode::Ok; });
        sess.switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE(sess.getData("/test_module:leafInt32")->asTerm().valueStr() == "123");

        // After using deleteItem, the leaf is no longer accesible from the operational datastore.
        sess.deleteItem("/test_module:leafInt32");
        sess.applyChanges();
        REQUIRE(!sess.getData("/test_module:leafInt32"));

        // Using discardItems makes the leaf visible again (in the operational datastore).
        sess.discardItems("/test_module:leafInt32");
        sess.applyChanges();
        REQUIRE(sess.getData("/test_module:leafInt32")->asTerm().valueStr() == "123");
    }

    DOCTEST_SUBCASE("edit batch")
    {
        auto data = sess.getData("/test_module:leafInt32");
        REQUIRE(!data);

        auto batch = sess.getContext().newPath("/test_module:leafInt32", "1230");
        sess.editBatch(batch, sysrepo::DefaultOperation::Merge);
        sess.applyChanges();
        data = sess.getData("/test_module:leafInt32");
        REQUIRE(data->asTerm().valueStr() == "1230");
    }

    DOCTEST_SUBCASE("switching datastore")
    {
        sess.switchDatastore(sysrepo::Datastore::Startup);
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Startup);
        sess.switchDatastore(sysrepo::Datastore::Candidate);
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Candidate);
        sess.switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Operational);
        sess.switchDatastore(sysrepo::Datastore::Running);
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Running);
    }

    DOCTEST_SUBCASE("Session::getConnection")
    {
        auto connection = sess.getConnection();
    }

    DOCTEST_SUBCASE("NACM")
    {
        // Before turning NACM on, we can set the value of the default-deny-all leaf.
        sess.setItem("/test_module:denyAllLeaf", "AHOJ");
        sess.applyChanges();
        // And also retrieve the value.
        auto data = sess.getData("/test_module:denyAllLeaf");
        REQUIRE(data.value().findPath("/test_module:denyAllLeaf").value().asTerm().valueStr() == "AHOJ");

        // check that repeated NACM initialization still works
        for (int i = 0; i < 3; ++i) {
            auto nacmSub = sess.initNacm();
            sess.setNacmUser("nobody");
            data = sess.getData("/test_module:denyAllLeaf");
            // After turning on NACM, we can't access the leaf.
            REQUIRE(!data);

            // And we can't set its value.
            sess.setItem("/test_module:denyAllLeaf", "someValue");
            REQUIRE_THROWS_WITH_AS(sess.applyChanges(),
                    "Session::applyChanges: Couldn't apply changes: SR_ERR_UNAUTHORIZED\n"
                    " Access to the data model \"test_module\" is denied because \"nobody\" NACM authorization failed. (SR_ERR_UNAUTHORIZED)\n"
                    " NETCONF: protocol: access-denied: /test_module:denyAllLeaf: "
                    "Access to the data model \"test_module\" is denied because \"nobody\" NACM authorization failed.",
                    sysrepo::ErrorWithCode);
        }

        // duplicate NACM initialization should throw
        auto nacm = sess.initNacm();
        REQUIRE_THROWS_WITH_AS(auto x = sess.initNacm(),
                "Couldn't initialize NACM: SR_ERR_INVAL_ARG\n"
                " Invalid arguments for function \"sr_nacm_init\". (SR_ERR_INVAL_ARG)",
                sysrepo::ErrorWithCode);
    }

    DOCTEST_SUBCASE("Session::getPendingChanges")
    {
        REQUIRE(sess.getPendingChanges() == std::nullopt);
        sess.setItem("/test_module:leafInt32", "123");
        REQUIRE(sess.getPendingChanges().value().findPath("/test_module:leafInt32")->asTerm().valueStr() == "123");

        DOCTEST_SUBCASE("apply")
        {
            sess.applyChanges();
        }

        DOCTEST_SUBCASE("discard")
        {
            sess.discardChanges();
        }

        REQUIRE(sess.getPendingChanges() == std::nullopt);
    }

    DOCTEST_SUBCASE("factory-default DS")
    {
        sess.switchDatastore(sysrepo::Datastore::FactoryDefault);
        auto data = sess.getData("/*");
        REQUIRE(*data->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings) == "{\n\n}\n");
        REQUIRE_THROWS_AS(sess.setItem("/test_module:leafInt32", "123"), sysrepo::ErrorWithCode);
    }

    DOCTEST_SUBCASE("session IDs")
    {
        REQUIRE(sess.getId() == sess.getId());
        REQUIRE(sess.getId() != conn->sessionStart().getId());
    }

    DOCTEST_SUBCASE("locking")
    {
        auto sid = sess.getId();

        {
            // L1 will be released at the scope exit
            auto l1 = sysrepo::Lock{sess};
            const auto start = std::chrono::steady_clock::now();
            try {
                // even though we provide a timeout, an attempt to lock by the same session is detected immediately
                auto l2 = sysrepo::Lock{sess, std::nullopt, 500ms};
                FAIL("should have thrown (immediately)");
            } catch (sysrepo::ErrorWithCode& e) {
                REQUIRE(e.code() == sysrepo::ErrorCode::Locked);
                std::string msg = e.what();
                REQUIRE(msg.find("already locked by this session " + std::to_string(sid)) != std::string::npos);
            }
            auto shouldBeImmediate = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            REQUIRE(shouldBeImmediate < 100);
        }
        {
            // ensure that L1 was released
            sysrepo::Lock l3{sess};
        }
        {
            sysrepo::Lock l4{sess, std::nullopt};
            const auto start = std::chrono::steady_clock::now();
            try {
                // locking through an unrelated session sleeps
                sysrepo::Lock{conn->sessionStart(), std::nullopt, 500ms};
                FAIL("should have thrown (after a timeout)");
            } catch (sysrepo::ErrorWithCode& e) {
                REQUIRE(e.code() == sysrepo::ErrorCode::Locked);
                std::string msg = e.what();
                REQUIRE(msg.find("is DS-locked by session " + std::to_string(sid)) != std::string::npos);
            }
            auto processingMS = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            REQUIRE(processingMS >= 500);
        }
        {
            sysrepo::Lock m1_lock{sess, "test_module"};
            sysrepo::Lock m2_lock{sess, "ietf-netconf-acm"};
        }

        // check that unlocking temporarily switches to the original DS, and then back to the current one
        {
            sysrepo::Lock l{sess};
            sess.switchDatastore(sysrepo::Datastore::FactoryDefault);
        }
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::FactoryDefault);
    }

    DOCTEST_SUBCASE("replace config")
    {
        REQUIRE(!sess.getData("/test_module:leafInt32"));
        // some "reasonable data" for two modules
        sess.setItem("/test_module:leafInt32", "666");
        sess.setItem("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']", "");
        sess.applyChanges();

        auto conf = sess.getData("/*");
        REQUIRE(!!conf);

        // override a single leaf
        REQUIRE(sess.getOneNode("/test_module:leafInt32").asTerm().valueStr() == "666");
        sess.setItem("/test_module:leafInt32", "123");
        sess.setItem("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']", "");
        sess.applyChanges();
        REQUIRE(sess.getOneNode("/test_module:leafInt32").asTerm().valueStr() == "123");

        DOCTEST_SUBCASE("this module empty config")
        {
            sess.replaceConfig(std::nullopt, "test_module");
            REQUIRE(!sess.getData("/test_module:leafInt32"));
            REQUIRE(sess.getOneNode("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']").asTerm().valueStr() == "foo");
            REQUIRE(sess.getOneNode("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']").asTerm().valueStr() == "bar");
        }

        DOCTEST_SUBCASE("this module")
        {
            sess.replaceConfig(conf, "test_module");
            REQUIRE(sess.getOneNode("/test_module:leafInt32").asTerm().valueStr() == "666");
            REQUIRE(sess.getOneNode("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']").asTerm().valueStr() == "foo");
            REQUIRE(sess.getOneNode("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']").asTerm().valueStr() == "bar");
        }

        DOCTEST_SUBCASE("other module")
        {
            sess.replaceConfig(std::nullopt, "ietf-netconf-acm");
            REQUIRE(sess.getOneNode("/test_module:leafInt32").asTerm().valueStr() == "123");
            REQUIRE(!sess.getData("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']"));
            REQUIRE(!sess.getData("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']"));
        }

        DOCTEST_SUBCASE("entire datastore empty config")
        {
            sess.replaceConfig(std::nullopt);
            REQUIRE(!sess.getData("/test_module:leafInt32"));
            REQUIRE(!sess.getData("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']"));
            REQUIRE(!sess.getData("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']"));
        }

        DOCTEST_SUBCASE("entire datastore")
        {
            sess.replaceConfig(conf);
            REQUIRE(sess.getOneNode("/test_module:leafInt32").asTerm().valueStr() == "666");
            REQUIRE(sess.getOneNode("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']").asTerm().valueStr() == "foo");
            REQUIRE(!sess.getData("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']"));
        }

        // the original tree is not corrupted
        REQUIRE(*conf->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings) != "");
    }

    DOCTEST_SUBCASE("libyang context flags")
    {
        sess.setItem("/test_module:popelnice/s", "666");
        REQUIRE(sess.getOneNode("/test_module:popelnice/s").asTerm().valueStr() == "666");
        // Parsed type info is not preserved by libyang unless its context is constructed with a flag,
        // and that flag is not used by sysrepo by default...
        REQUIRE_THROWS_AS(sess.getOneNode("/test_module:popelnice/s").schema().asLeaf().valueType().asString().length(), libyang::ParsedInfoUnavailable);

        // ...unless we pass that flag explicitly as a parameter to the connection.
        auto sess2 = sysrepo::Connection{sysrepo::ConnectionFlags::LibYangPrivParsed}.sessionStart();
        sess2.setItem("/test_module:popelnice/s", "333");
        REQUIRE(sess2.getOneNode("/test_module:popelnice/s").asTerm().valueStr() == "333");
        REQUIRE(sess2.getOneNode("/test_module:popelnice/s").schema().asLeaf().valueType().asString().length().parts[0].max == 10);
    }

    DOCTEST_SUBCASE("replay support")
    {
        REQUIRE_THROWS_WITH_AS(conn->getModuleReplaySupport("bla"), "Couldn't get replay support for module 'bla': SR_ERR_NOT_FOUND", std::runtime_error);
        REQUIRE_THROWS_WITH_AS(conn->setModuleReplaySupport("bla", true), "Couldn't set replay support for module 'bla': SR_ERR_NOT_FOUND", std::runtime_error);

        auto s = conn->getModuleReplaySupport("test_module");
        REQUIRE(!s.enabled);
        REQUIRE(!s.earliestNotification);

        conn->setModuleReplaySupport("test_module", true);
        s = conn->getModuleReplaySupport("test_module");
        REQUIRE(s.enabled);
        REQUIRE(!s.earliestNotification);

        auto notification = sess.getContext().newPath("/test_module:ping");
        notification.newPath("myLeaf", "132");
        sess.sendNotification(notification, sysrepo::Wait::Yes);

        s = conn->getModuleReplaySupport("test_module");
        REQUIRE(s.enabled);
        REQUIRE(s.earliestNotification);
    }
}
