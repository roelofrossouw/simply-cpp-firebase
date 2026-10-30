# simply-cpp-firebase

`sc-firebase` talks to Google Firebase from a server, the way Firebase's Admin
SDKs do, without them: a service account's key becomes OAuth2 access tokens,
and the Realtime Database is read and written over its REST API. It needs
sc-core (its HTTP client), nlohmann/json and OpenSSL.

**Documentation:** the [simply-cpp wiki](https://github.com/roelofrossouw/simply-cpp/wiki) has a [Firebase guide](https://github.com/roelofrossouw/simply-cpp/wiki/Firebase) and the [sc-firebase reference](https://github.com/roelofrossouw/simply-cpp/wiki/Reference-sc-firebase).

```cpp
#include <sc_firebase.h>

const auto account = sc::firebase::service_account::load("service-account.json");
const sc::firebase::database db{"https://example-app.firebaseio.com", account};

db.set("devices/42", {{"lat", -26.2}, {"lng", 28.0}, {"online", 1}});
const auto device = db.get("devices/42");                  // JSON; null when there's nothing
const auto ids = db.keys("devices");                       // child keys only (a shallow get)
db.update("devices", {{"42/online", 0}, {"43", nullptr}}); // several paths at once; null removes
const auto key = db.push("log", {{"event", "moved"}});      // a new child, Firebase's key
db.move("accounts/1/42", "accounts/2/42");                 // in one update: never in both or neither
db.remove("devices/43");
```

| Type | | |
|---|---|---|
| `sc::firebase::service_account` | `parse(text)`, `load(file)`; `project_id`, `client_email`, `private_key`, `private_key_id`, `token_uri` | the key the Firebase console downloads |
| `sc::firebase::credentials` | `access_token()`, `forget()`, `assertion(now)`, `default_scopes()` | OAuth2 access tokens, kept until near their end |
| `sc::firebase::database` | `get(path, shallow)`, `keys()`, `set()`, `update()`, `push()`, `remove()`, `move(from, to, overwrite)`, `url_of()`, `timeout()` | a Realtime Database |
| `sc::firebase::error` | `status()`, `what()` | a refused request, or none answered (status 0) |

- **The key:** in the Firebase console, Project settings, Service accounts,
  Generate new private key. It's a JSON file; keep it out of source code and
  readable by its program's user only.
- **Tokens:** a JWT signed with the key (RS256) is traded at Google's token URI
  for an access token that lasts an hour; it's kept until five minutes before
  its end. One `credentials` can be shared (`std::shared_ptr`) by several
  `database` objects and by threads. A 401 from the database gets one new token
  and one retry.
- **Paths** are `/`-separated, without `.json`; extra slashes don't matter and
  `""` is the root. Each segment is percent-encoded in the URL.
- **move()** reads the source, checks the target (unless `overwrite`), then
  writes both in one multi-path update of their common parent: the database
  applies it all or not at all. It returns false when there was nothing to move
  or the target was taken.
- **Errors:** every call throws `sc::firebase::error` with Google's message
  ("Permission denied", "invalid_grant: ...") and the HTTP status.
- Not yet: Cloud Messaging (push notifications), Firestore, verifying users' ID
  tokens, and listening for changes (the REST API's event stream).

## The demo

```bash
sc-firebase-demo -c service-account.json -u https://example-app.firebaseio.com get devices/42
sc-firebase-demo -c service-account.json -u https://example-app.firebaseio.com keys devices
sc-firebase-demo -c service-account.json -u https://example-app.firebaseio.com set devices/42 '{"online": 1}'
sc-firebase-demo -c service-account.json -u https://example-app.firebaseio.com move devices/42 archive/42
```

## Building

`scripts/install.sh` builds, tests and installs the module. The unit test needs
no network. `test-firebase-live` writes to a real database under a path of its
own and removes it after; it runs when `SC_FIREBASE_TEST_CREDENTIALS` (a service
account's file) and `SC_FIREBASE_TEST_URL` are set (and
`SC_FIREBASE_TEST_ROOT`, default `sc-firebase-test`), and skips otherwise.

## License

Apache License 2.0 (see `LICENSE` and `NOTICE`). OpenSSL and libcurl are linked
from the system.
