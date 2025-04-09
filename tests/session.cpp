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
    const auto leaf = "/test_module:leafInt32"s;

    DOCTEST_SUBCASE("Session should be still valid even after the Connection class gets freed")
    {
        conn = std::nullopt;
        REQUIRE(sess.activeDatastore() == sysrepo::Datastore::Running);
    }

    DOCTEST_SUBCASE("Session lifetime is prolonged with data from getData")
    {
        sess.setItem(leaf, "123");
        sess.applyChanges();
        auto data = sysrepo::Connection{}.sessionStart().getData(leaf);
        REQUIRE(data->asTerm().valueStr() == "123");
    }

    DOCTEST_SUBCASE("basic data manipulation")
    {
        auto data = sess.getData(leaf);
        REQUIRE(!data);

        sess.setItem(leaf, "123");
        sess.applyChanges();
        data = sess.getData(leaf);
        REQUIRE(data->asTerm().valueStr() == "123");
        auto node = sess.getOneNode(leaf);
        REQUIRE(node.asTerm().valueStr() == "123");

        sess.setItem(leaf, "420");
        sess.applyChanges();
        data = sess.getData(leaf);
        REQUIRE(data->asTerm().valueStr() == "420");

        sess.deleteItem(leaf);
        sess.applyChanges();
        data = sess.getData(leaf);
        REQUIRE(!data);

        sess.setItem(leaf, "123");
        sess.discardChanges();
        data = sess.getData(leaf);
        REQUIRE(!data);
        REQUIRE_THROWS_WITH_AS(sess.getOneNode(leaf),
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
  "test_module:popelnice": {}
}
)");

            data = sess.getData("/test_module:popelnice", 2);
            REQUIRE(data);
            REQUIRE(*data->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::KeepEmptyCont) == R"({
  "test_module:popelnice": {
    "content": {}
  }
}
)");

            // If a list should be returned, its keys are always returned as well.
            data = sess.getData("/test_module:popelnice", 3);
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

            data = sess.getData("/test_module:popelnice", 4);
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
            sess.setItem(leaf, "1");
            sess.applyChanges();

            DOCTEST_SUBCASE("Default options")
            {
                auto data = sess.getData("/test_module:*");
                REQUIRE(data);
                REQUIRE(data->findPath("/test_module:stateLeaf"));
                REQUIRE(data->findPath(leaf));
            }

            DOCTEST_SUBCASE("No state data")
            {
                auto data = sess.getData("/test_module:*", 0, sysrepo::GetOptions::OperNoState);
                REQUIRE(data);
                REQUIRE(!data->findPath("/test_module:stateLeaf"));
                REQUIRE(data->findPath(leaf));
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

    DOCTEST_SUBCASE("push operational data and deleting stuff")
    {
        // Set some arbitrary leaf.
        sess.setItem(leaf, "123");
        sess.applyChanges();

        // The leaf is accesible from the running datastore.
        REQUIRE(sess.getData(leaf)->asTerm().valueStr() == "123");

        // The leaf is NOT accesible from the operational datastore without a subscription.
        sess.switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE(!sess.getData(leaf));

        // When we create a subscription, the leaf is accesible from the operational datastore.
        sess.switchDatastore(sysrepo::Datastore::Running);
        auto sub = sess.onModuleChange("test_module", [] (auto, auto, auto, auto, auto, auto) { return sysrepo::ErrorCode::Ok; });
        sess.switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE(sess.getData(leaf)->asTerm().valueStr() == "123");

        DOCTEST_SUBCASE("discardOperationalChanges")
        {
            // apply a change which makes the leaf disappear
            sess.dropForeignOperationalContent(leaf);
            REQUIRE(!!sess.getData(leaf));
            sess.applyChanges();
            REQUIRE(!sess.getData(leaf));

            // check that a magic sysrepo:discard-items node is in place
            REQUIRE(!!sess.operationalChanges());
            auto matchingDiscard = sysrepo::findMatchingDiscard(*sess.operationalChanges(), leaf);
            REQUIRE(!!matchingDiscard);
            REQUIRE(matchingDiscard->value() == leaf);
            REQUIRE(matchingDiscard->name().moduleOrNamespace == "sysrepo");
            REQUIRE(matchingDiscard->name().name == "discard-items");
            REQUIRE(!sysrepo::findMatchingDiscard(*sess.operationalChanges(), "something else"));

            DOCTEST_SUBCASE("forget changes via discardOperationalChanges(module)")
            {
                // Using discardOperationalChanges makes the leaf visible again (in the operational datastore).
                // Also, no need to applyChanges().
                sess.discardOperationalChanges("test_module");
            }

            DOCTEST_SUBCASE("forget changes via a selective edit")
            {
                // this edit only has a single node, which means that we cannot really call unlink() and hope for a sane result
                REQUIRE(matchingDiscard->firstSibling() == *matchingDiscard);

                // so, we add a dummy node instead...
                auto root = matchingDiscard->newPath("/test_module:popelnice/s", "foo");
                // ...and only then we nuke the eixtsing discard-items node
                matchingDiscard->unlink();
                sess.editBatch(*root, sysrepo::DefaultOperation::Replace);
                sess.applyChanges();
            }

            DOCTEST_SUBCASE("multiple sysrepo:discard-items nodes")
            {
                sess.dropForeignOperationalContent("/test_module:popelnice");
                sess.dropForeignOperationalContent("/test_module:popelnice/s");
                sess.dropForeignOperationalContent("/test_module:values");
                sess.dropForeignOperationalContent("/test_module:popelnice/content");
                sess.dropForeignOperationalContent("/test_module:denyAllLeaf");
                sess.dropForeignOperationalContent(leaf); // yup, once more, in addition to the one at the very beginning
                sess.applyChanges();

                auto forPopelnice = sysrepo::findMatchingDiscard(*sess.operationalChanges(), "/test_module:popelnice");
                REQUIRE(!!forPopelnice);
                REQUIRE(forPopelnice->value() == "/test_module:popelnice");
                auto oneMatch = sysrepo::findMatchingDiscard(*sess.operationalChanges(), "/test_module:values");
                REQUIRE(!!oneMatch);
                REQUIRE(oneMatch->value() == "/test_module:values");

                auto atOrBelowPopelnice = sysrepo::findMatchingDiscardPrefixes(*sess.operationalChanges(), "/test_module:popelnice");
                REQUIRE(atOrBelowPopelnice.size() == 3);
                // yup, these are apparently backwards compared to how I put them in. Never mind.
                REQUIRE(atOrBelowPopelnice[2].value() == "/test_module:popelnice");
                REQUIRE(atOrBelowPopelnice[1].value() == "/test_module:popelnice/s");
                REQUIRE(atOrBelowPopelnice[0].value() == "/test_module:popelnice/content");

                auto belowPopelnice = sysrepo::findMatchingDiscardPrefixes(*sess.operationalChanges(), "/test_module:popelnice/");
                REQUIRE(belowPopelnice.size() == 2);
                // again, the order is reversed
                REQUIRE(belowPopelnice[1].value() == "/test_module:popelnice/s");
                REQUIRE(belowPopelnice[0].value() == "/test_module:popelnice/content");

                auto newEdit = sess.operationalChanges();
                auto forLeaf = sysrepo::findMatchingDiscardPrefixes(*newEdit, leaf);
                REQUIRE(forLeaf.size() == 2);
                REQUIRE(forLeaf[0].value() == leaf);
                REQUIRE(forLeaf[1].value() == leaf);
                for (auto node : forLeaf) {
                    sysrepo::unlinkFromForest(newEdit, node);
                }
                sess.editBatch(*newEdit, sysrepo::DefaultOperation::Replace);
                sess.applyChanges();
            }

            REQUIRE(sess.getData(leaf)->asTerm().valueStr() == "123");
        }

        DOCTEST_SUBCASE("direct edit of a libyang::DataNode")
        {
            // at first, set the leaf to some random value
            sess.setItem(leaf, "456");
            sess.applyChanges();
            REQUIRE(sess.getData(leaf)->asTerm().valueStr() == "456");

            // change the edit in-place
            auto pushed = sess.operationalChanges();
            REQUIRE(pushed->path() == leaf);
            pushed->asTerm().changeValue("666");
            sess.editBatch(*pushed, sysrepo::DefaultOperation::Replace);
            sess.applyChanges();
            REQUIRE(sess.getData(leaf)->asTerm().valueStr() == "666");

            // Remove that previous edit in-place. Since the new edit cannot be empty, set some other leaf.
            pushed = sess.operationalChanges();
            auto another = "/test_module:popelnice/s"s;
            pushed->newPath(another, "xxx");
            pushed = *pushed->findPath(another);
            pushed->findPath(leaf)->unlink();
            // "the edit" for sysrepo must refer to a top-level node
            while (pushed->parent()) {
                pushed = *pushed->parent();
            }
            REQUIRE(!pushed->findPath(leaf));
            REQUIRE(!!pushed->findPath(another));
            sess.editBatch(*pushed, sysrepo::DefaultOperation::Replace);
            sess.applyChanges();
            REQUIRE(sess.getData(leaf)->asTerm().valueStr() == "123");
        }
    }

    DOCTEST_SUBCASE("edit batch")
    {
        auto data = sess.getData(leaf);
        REQUIRE(!data);

        auto batch = sess.getContext().newPath(leaf, "1230");
        sess.editBatch(batch, sysrepo::DefaultOperation::Merge);
        sess.applyChanges();
        data = sess.getData(leaf);
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

        REQUIRE(!sess.getNacmUser());

        // check that repeated NACM initialization still works
        for (int i = 0; i < 3; ++i) {
            auto nacmSub = sess.initNacm();
            sess.setNacmUser("nobody");
            REQUIRE(sess.getNacmUser() == "nobody");

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

        REQUIRE(!!sess.getNacmUser());
        REQUIRE(sess.getNacmUser() == "nobody");

        // duplicate NACM initialization should throw
        auto nacm = sess.initNacm();
        REQUIRE_THROWS_WITH_AS(auto x = sess.initNacm(),
                "Couldn't initialize NACM: SR_ERR_INVAL_ARG\n"
                " Invalid arguments for function \"sr_nacm_init\". (SR_ERR_INVAL_ARG)",
                sysrepo::ErrorWithCode);
    }

    DOCTEST_SUBCASE("Session::checkNacmOperation")
    {
        auto nacmSub = sess.initNacm();

        // check NACM access for RPCs
        auto shutdownRPC = sess.getContext().newPath("/test_module:shutdown", std::nullopt);
        auto denyAllRPC = sess.getContext().newPath("/test_module:deny-all-rpc", std::nullopt);

        // user not set, everything is permitted
        REQUIRE(sess.checkNacmOperation(shutdownRPC) == true);
        REQUIRE(sess.checkNacmOperation(denyAllRPC) == true);
        REQUIRE(sess.getErrors().size() == 0);

        sess.setNacmUser("root");
        REQUIRE(sess.checkNacmOperation(shutdownRPC) == true);
        REQUIRE(sess.checkNacmOperation(denyAllRPC) == true);
        REQUIRE(sess.getErrors().size() == 0);

        sess.setNacmUser("nobody");
        REQUIRE(sess.checkNacmOperation(shutdownRPC) == true);
        REQUIRE(sess.checkNacmOperation(denyAllRPC) == false);
        REQUIRE(sess.getErrors().size() == 1);
        REQUIRE(sess.getErrors().at(0) == sysrepo::ErrorInfo{
                    .code = sysrepo::ErrorCode::Unauthorized,
                    .errorMessage = "Executing the operation is denied because \"nobody\" NACM authorization failed.",
                });

        sess.setNacmUser("root"); // 'nobody' is not authorized to write into this subtree
        sess.switchDatastore(sysrepo::Datastore::Running);
        sess.setItem("/ietf-netconf-acm:nacm/enable-external-groups", "false");
        sess.setItem("/ietf-netconf-acm:nacm/groups/group[name='grp']/user-name[.='nobody']", "");
        sess.setItem("/ietf-netconf-acm:nacm/rule-list[name='rule']/group[.='grp']", "");
        sess.setItem("/ietf-netconf-acm:nacm/rule-list[name='rule']/rule[name='1']/module-name", "test_module");
        sess.setItem("/ietf-netconf-acm:nacm/rule-list[name='rule']/rule[name='1']/access-operations", "*");
        sess.setItem("/ietf-netconf-acm:nacm/rule-list[name='rule']/rule[name='1']/action", "deny");
        sess.applyChanges();

        sess.setNacmUser("root");
        REQUIRE(sess.checkNacmOperation(denyAllRPC) == true);
        REQUIRE(sess.checkNacmOperation(shutdownRPC) == true);

        sess.setNacmUser("nobody");
        REQUIRE(sess.checkNacmOperation(denyAllRPC) == false);
        REQUIRE(sess.checkNacmOperation(shutdownRPC) == false);
    }

    DOCTEST_SUBCASE("Session::getPendingChanges")
    {
        REQUIRE(sess.getPendingChanges() == std::nullopt);
        sess.setItem(leaf, "123");
        REQUIRE(sess.getPendingChanges().value().findPath(leaf)->asTerm().valueStr() == "123");

        DOCTEST_SUBCASE("apply")
        {
            sess.applyChanges();
        }

        DOCTEST_SUBCASE("discard")
        {
            sess.discardChanges();
        }

        DOCTEST_SUBCASE("discard XPath")
        {
            sess.discardChanges(leaf);
        }

        REQUIRE(sess.getPendingChanges() == std::nullopt);
    }

    DOCTEST_SUBCASE("factory-default DS")
    {
        sess.switchDatastore(sysrepo::Datastore::FactoryDefault);
        auto data = sess.getData("/*");
        REQUIRE(*data->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::WithSiblings) == "{\n\n}\n");
        REQUIRE_THROWS_AS(sess.setItem(leaf, "123"), sysrepo::ErrorWithCode);
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
        REQUIRE(!sess.getData(leaf));
        // some "reasonable data" for two modules
        sess.setItem(leaf, "666");
        sess.setItem("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']", "");
        sess.applyChanges();

        auto conf = sess.getData("/*");
        REQUIRE(!!conf);

        // override a single leaf
        REQUIRE(sess.getOneNode(leaf).asTerm().valueStr() == "666");
        sess.setItem(leaf, "123");
        sess.setItem("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']", "");
        sess.applyChanges();
        REQUIRE(sess.getOneNode(leaf).asTerm().valueStr() == "123");

        DOCTEST_SUBCASE("this module empty config")
        {
            sess.replaceConfig(std::nullopt, "test_module");
            REQUIRE(!sess.getData(leaf));
            REQUIRE(sess.getOneNode("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']").asTerm().valueStr() == "foo");
            REQUIRE(sess.getOneNode("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']").asTerm().valueStr() == "bar");
        }

        DOCTEST_SUBCASE("this module")
        {
            sess.replaceConfig(conf, "test_module");
            REQUIRE(sess.getOneNode(leaf).asTerm().valueStr() == "666");
            REQUIRE(sess.getOneNode("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']").asTerm().valueStr() == "foo");
            REQUIRE(sess.getOneNode("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']").asTerm().valueStr() == "bar");
        }

        DOCTEST_SUBCASE("other module")
        {
            sess.replaceConfig(std::nullopt, "ietf-netconf-acm");
            REQUIRE(sess.getOneNode(leaf).asTerm().valueStr() == "123");
            REQUIRE(!sess.getData("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']"));
            REQUIRE(!sess.getData("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']"));
        }

        DOCTEST_SUBCASE("entire datastore empty config")
        {
            sess.replaceConfig(std::nullopt);
            REQUIRE(!sess.getData(leaf));
            REQUIRE(!sess.getData("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='foo']"));
            REQUIRE(!sess.getData("/ietf-netconf-acm:nacm/groups/group[name='ahoj']/user-name[.='bar']"));
        }

        DOCTEST_SUBCASE("entire datastore")
        {
            sess.replaceConfig(conf);
            REQUIRE(sess.getOneNode(leaf).asTerm().valueStr() == "666");
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
