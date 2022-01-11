# C++ bindings for sysrepo

![License](https://img.shields.io/github/license/sysrepo/sysrepo-cpp)
[![Gerrit](https://img.shields.io/badge/patches-via%20Gerrit-blue)](https://gerrit.cesnet.cz/q/project:CzechLight/sysrepo-cpp)
[![Zuul CI](https://img.shields.io/badge/zuul-checked-blue)](https://zuul.gerrit.cesnet.cz/t/public/buildsets?project=CzechLight/sysrepo-cpp)

*sysrepo-cpp* are object-oriented bindings of the [*sysrepo*](https://github.com/sysrepo/sysrepo) library.
It uses RAII for automatic memory management.

## Dependencies
- [sysrepo](https://github.com/sysrepo/sysrepo) - the `devel` branch (even for the `master` branch of *sysrepo-cpp*)
- [libyang-cpp](https://github.com/CESNET/libyang-cpp) - C++ bindings for *libyang*
- C++20 compiler

## Building
*sysrepo-cpp* uses *CMake* for building. The standard way of building *sysrepo-cpp* looks like this:
```
mkdir build
cd build
cmake ..
make
make install
```
## Usage
### Differences from the previous sysrepo C++ bindings
- Most of the classes in *sysrepo-cpp* are not directly instantiated by the user, and are instead returned by methods.
  The only class directly instantiated by the user is the `sysrepo::Connection` class.
- The classes are no longer wrapped by `std::shared_ptr`. However, they are still copyable and one can have multiple
  instances of, i.e., `sysrepo::Session` that refer to the same session. Memory management will still work
  automatically.

### Connections and sessions
The core classes for *sysrepo-cpp* are `sysrepo::Connection` and `sysrepo::Session`. Creating those is straightforward.
```cpp
auto session = sysrepo::Connection{}.sessionStart();
```
### Examples of simple usage
These are some of the most typical operations supported by sysrepo-cpp.

#### Editing data and retrieving data
```cpp
auto session = sysrepo::Connection{}.sessionStart();
session.setItem("/module:myLeaf", "some-value");
session.applyChanges();

auto data = session.getData("/module:leaf");
if (data) {
    std::cout << data->asTerm().valueStr() << "\n";
} else {
    std::cout << "no data\n";
}
```

#### Creating a subscription for module changes
```cpp
sysrepo::ModuleChangeCb moduleChangeCb = [] (auto session, auto, auto, auto, auto, auto) {
    for (const auto& change : session.getChanges()) {
        // do stuff
    }
    return sysrepo::ErrorCode::Ok;
};

auto session = sysrepo::Connection{}.sessionStart();
auto sub = sess.onModuleChange("my-module-name", moduleChangeCb);
sub.onRPCAction(...);
```

#### Using sysrepo-cpp in your classes
In C++, one usually wants to create a class that groups all of the *sysrepo-cpp* classes, mainly sessions
and subscriptions. Here is an example of such class:
```cpp
class SysrepoManager {
public:
    SysrepoManager()
        : m_sess(sysrepo::Connection{}.sessionStart())
        , m_sub(/*???*/) // How to initialize this?
    {
    }

private:
    sysrepo::Session m_sess;
    sysrepo::Subscription m_sub;
};
```
The example illustrates a small caveat of the `sysrepo::Subscription` class: it is not possible to have an "empty"
instance of it. Every `sysrepo::Subscription` instance must already point to a valid subscription. There are two ways of
solving this:
- Just initialize the `m_sub` field in the member initializer list. This works if one wants to immediately subscribe to
  something on the creation of the example class.
- If actual empty subscriptions are needed `std::optional<sysrepo::Subscription>` can be used as the type of `m_sub`.
  This enables having no active subscriptions, however, when actually creating subscriptions, the first call must use
  `m_sess` and any following should use the `m_sub`. Example:
  ```cpp
  m_sub = m_sess.onModuleChange(...);
  m_sub.onRPCAction(...);
  m_sub.onModuleChange(...);
  ```

For more examples, check out the `examples/` and the `tests/` directory.

## Contributing
The development is being done on Gerrit [here](https://gerrit.cesnet.cz/q/project:CzechLight/sysrepo-cpp). Instructions
on how to submit patches can be found
[here](https://gerrit.cesnet.cz/Documentation/intro-gerrit-walkthrough-github.html). GitHub Pull Requests are not used.
