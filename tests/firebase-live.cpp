// sc-firebase against a real Realtime Database: a token, then set, get (deep and shallow), update
// of several paths at once, push, move and remove, under a path of its own that it removes after.
// Needs SC_FIREBASE_TEST_CREDENTIALS (a service account's JSON file) and SC_FIREBASE_TEST_URL (the
// database's URL); SC_FIREBASE_TEST_ROOT is where it writes (default "sc-firebase-test"). Without
// them it passes, saying it skipped.
#include "sc_firebase.h"
#include "sc_test.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

int main() {
    const char *credentials_file = std::getenv("SC_FIREBASE_TEST_CREDENTIALS");
    const char *url = std::getenv("SC_FIREBASE_TEST_URL");
    if (!credentials_file || !url) {
        std::cout << "Skipped: set SC_FIREBASE_TEST_CREDENTIALS and SC_FIREBASE_TEST_URL to test against a real database\n";
        return 0;
    }
    const char *root_variable = std::getenv("SC_FIREBASE_TEST_ROOT");
    const std::string base = std::string{root_variable ? root_variable : "sc-firebase-test"} + "/run-" + std::to_string(std::random_device{}());

    const auto access = std::make_shared<sc::firebase::credentials>(sc::firebase::service_account::load(credentials_file));
    const sc::firebase::database db{url, access};

    SECTION("An access token");
    CHECK(!access->access_token().empty());
    const auto token = access->access_token();
    CHECK_EQ(access->access_token(), token); // kept, not asked for again

    SECTION("set and get");
    {
        db.set(base + "/accounts/1/350000000000001", {{"lat", -26.2}, {"lng", 28.0}, {"online", 1}});
        db.set(base + "/accounts/1/350000000000002", {{"lat", -33.9}, {"lng", 18.4}, {"online", 0}});
        CHECK_EQ(db.get(base + "/accounts/1/350000000000001")["online"].get<int>(), 1);
        CHECK(db.get(base + "/nothing/here").is_null());
        CHECK(db.keys(base + "/accounts/1") == (std::vector<std::string>{"350000000000001", "350000000000002"}));
        const auto shallow = db.get(base + "/accounts/1", true);
        CHECK(shallow["350000000000001"] == true);
    }

    SECTION("update: several paths at once, null removes");
    {
        db.update(base + "/accounts/1", {{"350000000000001/online", 0}, {"350000000000002", nullptr}, {"timestamp", "2026-10-30T12:00:00"}});
        CHECK_EQ(db.get(base + "/accounts/1/350000000000001/online").get<int>(), 0);
        CHECK_EQ(db.get(base + "/accounts/1/350000000000001/lat").get<double>(), -26.2); // left as it was
        CHECK(db.get(base + "/accounts/1/350000000000002").is_null());
        CHECK_EQ(db.get(base + "/accounts/1/timestamp").get<std::string>(), std::string{"2026-10-30T12:00:00"});
    }

    SECTION("push: a new key");
    {
        const auto key = db.push(base + "/log", {{"event", "moved"}});
        CHECK(!key.empty());
        CHECK_EQ(db.get(base + "/log/" + key + "/event").get<std::string>(), std::string{"moved"});
    }

    SECTION("move: to another account, not over what's there unless asked");
    {
        CHECK(db.move(base + "/accounts/1/350000000000001", base + "/accounts/2/350000000000001"));
        CHECK(db.get(base + "/accounts/1/350000000000001").is_null());
        CHECK_EQ(db.get(base + "/accounts/2/350000000000001/lat").get<double>(), -26.2);
        CHECK(!db.move(base + "/accounts/1/350000000000001", base + "/accounts/3/350000000000001")); // nothing there
        db.set(base + "/accounts/1/350000000000001", {{"lat", 1}});
        CHECK(!db.move(base + "/accounts/1/350000000000001", base + "/accounts/2/350000000000001")); // taken
        CHECK(db.move(base + "/accounts/1/350000000000001", base + "/accounts/2/350000000000001", true));
        CHECK_EQ(db.get(base + "/accounts/2/350000000000001/lat").get<int>(), 1);
    }

    SECTION("remove");
    {
        db.remove(base);
        CHECK(db.get(base).is_null());
    }

    SECTION("A refused request throws, with its status");
    {
        const sc::firebase::database elsewhere{"https://sc-firebase-no-such-database-example.firebaseio.com", access};
        bool threw = false;
        try {
            (void) elsewhere.get("x");
        } catch (const sc::firebase::error &error) {
            threw = true;
            std::cout << "  (as expected: " << error.what() << ")\n";
        }
        CHECK(threw);
    }

    TEST_SUMMARY();
}
