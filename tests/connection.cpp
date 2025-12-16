#include <doctest/doctest.h>
#include <sysrepo-cpp/utils/exception.hpp>
#include <sysrepo-cpp/utils/utils.hpp>

#include "tests/configure.cmake.h"

using namespace std::literals;

TEST_CASE("connection")
{
    sysrepo::setLogLevelStderr(sysrepo::LogLevel::Information);
    std::optional<sysrepo::Connection> conn{std::in_place};
    auto sess = conn->sessionStart();
    sess.copyConfig(sysrepo::Datastore::Startup);
    const auto leaf = "/test_module:leafInt32"s;

    DOCTEST_SUBCASE("Connection::installModules, Connection::removeModules")
    {
        std::filesystem::path dir{CMAKE_CURRENT_SOURCE_DIR};
        std::filesystem::path path = dir / "tests" / "test_module.yang";
        std::string schema = R"(module dummy { namespace "http://dummy.com"; prefix "dummy"; })";
        std::vector<struct sysrepo::ModuleInstallation> modules;
        std::variant<std::monostate, std::filesystem::path, std::string> initData;
        libyang::DataFormat format;
        std::vector<std::string> toRemove;

        DOCTEST_SUBCASE("One module, empty features, no initial data")
        {
            modules = {{.schema = path}};
            initData = {};
            format = libyang::DataFormat::Detect;
            toRemove = {"test_module"};
        }

        DOCTEST_SUBCASE("Two modules, two features, initial data")
        {
            modules = {{.schema = path, .features = {"dummy", "dummy2"}}, {.schema = schema}};
            initData = std::string("<leafInt32 xmlns=\"http://example.com\">2</leafInt32>");
            format = libyang::DataFormat::XML;
            toRemove = {"test_module", "dummy"};
        }

        // Check that it is actually gone!
        REQUIRE_THROWS_WITH_AS(sess.getOneNode(leaf),
                               "Session::getOneNode: Couldn't get '/test_module:leafInt32': SR_ERR_LY\n"
                               " Unknown/non-implemented module \"test_module\". (SR_ERR_LY)",
                               sysrepo::ErrorWithCode);

        // Install the modules
        conn->installModules(modules, dir, initData, format);

        // Check that saving module data works
        sess.setItem(leaf, "1");
        sess.applyChanges();
        REQUIRE(sess.getData(leaf));

        // Remove the modules
        conn->removeModules(toRemove);
    }
}
