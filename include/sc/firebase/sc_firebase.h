#ifndef SC_FIREBASE_H
#define SC_FIREBASE_H
// Google Firebase from C++: a service account's access tokens, and the Realtime Database over its
// REST API (get, set, update, push, remove, and a move that can't leave a node in both places or
// neither). What Firebase's Admin SDKs do for a server, without them.
//
//     const auto account = sc::firebase::service_account::load("service-account.json");
//     sc::firebase::database db{"https://example-app.firebaseio.com", account};
//     db.set("devices/42", {{"lat", -26.2}, {"lng", 28.0}});
//     const auto device = db.get("devices/42");          // {"lat": -26.2, "lng": 28.0}
//     db.update("devices", {{"42/online", 1}, {"43", nullptr}});   // two paths at once; null removes
//     db.move("devices/42", "archive/42");
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sc::firebase {
    using json = nlohmann::json;

    // A request Google refused, or one that got no answer (status 0). what() has Google's message.
    class error : public std::runtime_error {
    public:
        error(long status, const std::string &message) : std::runtime_error(message), status_(status) {}

        // The HTTP status: 401, 403, 404 ...; 0 when no answer came.
        [[nodiscard]] long status() const noexcept { return status_; }

    private:
        long status_;
    };

    // A service account's key, as the Firebase console downloads it (Project settings, Service
    // accounts, Generate new private key): the JSON file with project_id, client_email,
    // private_key ... Keep it out of source code and readable by its user only.
    struct service_account {
        std::string project_id;
        std::string client_email;
        std::string private_key;    // PEM
        std::string private_key_id;
        std::string token_uri = "https://oauth2.googleapis.com/token";

        // Throws std::invalid_argument for JSON without client_email or private_key.
        static service_account parse(std::string_view text);
        // Throws std::runtime_error when the file can't be read, else as parse().
        static service_account load(const std::filesystem::path &file);
    };

    // OAuth2 access tokens for a service account: a JWT signed with its key (RS256) traded at its
    // token_uri for a token that lasts an hour, kept until five minutes before it runs out. One
    // object can be shared by threads and by several database objects.
    class credentials {
    public:
        // The scopes the Realtime Database wants, and Google's userinfo.email that it checks.
        static const std::vector<std::string> &default_scopes();

        explicit credentials(service_account account, std::vector<std::string> scopes = default_scopes());

        // A valid access token, asking Google for a new one when the last is near its end. Throws
        // sc::firebase::error when Google refuses (a revoked key: 400 invalid_grant).
        std::string access_token();

        // Drops the kept token, so the next access_token() asks again (after a 401, say).
        void forget();

        [[nodiscard]] const service_account &account() const noexcept { return account_; }

        // The signed request for a token (the JWT, "header.claims.signature"), issued at `now`.
        // Throws std::invalid_argument when the private key can't be read.
        [[nodiscard]] std::string assertion(std::chrono::system_clock::time_point now) const;

    private:
        service_account account_;
        std::vector<std::string> scopes_;
        std::mutex mutex_;
        std::string token_;
        std::chrono::system_clock::time_point expires_{};
    };

    // A Firebase Realtime Database, over its REST API. Paths are '/'-separated, without the
    // ".json"; leading and trailing '/' don't matter, and "" (or "/") is the root. Values are JSON:
    // null for nothing. Each call is one HTTPS request, signed with the credentials' token; a 401
    // gets one new token and one retry. Methods throw sc::firebase::error when the database
    // refuses (its message: "Permission denied" ...) or doesn't answer. Usable from several threads.
    class database {
    public:
        // url: "https://<name>.firebaseio.com" or "https://<name>.<region>.firebasedatabase.app",
        // as the console shows it; a trailing '/' doesn't matter.
        database(std::string url, std::shared_ptr<credentials> access);
        database(std::string url, service_account account);

        // The value at path; null when there's nothing. shallow: an object's values as true
        // (its children's keys only), not the whole tree.
        [[nodiscard]] json get(std::string_view path, bool shallow = false) const;
        // The child keys at path (a shallow get), sorted; empty when there's nothing or a value.
        [[nodiscard]] std::vector<std::string> keys(std::string_view path) const;

        // Replaces what's at path with value (null removes it).
        void set(std::string_view path, const json &value) const;
        // Writes each of values' members under path, leaving the others: {"a": 1} sets path/a. A
        // member's name may be a path itself ("42/online"), so one call changes several places at
        // once, all or none; null removes.
        void update(std::string_view path, const json &values) const;
        // Adds value as a new child of path, under a key Firebase makes (time ordered); the key.
        std::string push(std::string_view path, const json &value) const;
        // Removes what's at path.
        void remove(std::string_view path) const;

        // Moves what's at from to to, in one update of their common parent, so it's never in both
        // places or neither. False when there's nothing at from, or (unless overwrite) something
        // at to already; true when moved.
        bool move(std::string_view from, std::string_view to, bool overwrite = false) const;

        // The REST URL of path: the database's URL, the path's segments percent-encoded, ".json",
        // and the query ("shallow=true"), if any.
        [[nodiscard]] std::string url_of(std::string_view path, std::string_view query = {}) const;

        [[nodiscard]] const std::string &url() const noexcept { return url_; }

        // Seconds a request may take (default 30).
        void timeout(long seconds) noexcept { timeout_ = seconds; }

    private:
        // One request; the body parsed as JSON. method: GET, PUT, POST, PATCH or DELETE.
        json request(const char *method, std::string_view path, const json *body, std::string_view query = {}) const;

        std::string url_;
        std::shared_ptr<credentials> access_;
        long timeout_ = 30;
    };

    // A path's segments: "/a//b/" -> {"a", "b"}.
    std::vector<std::string> split_path(std::string_view path);
    // The segments joined by '/': {"a", "b"} -> "a/b".
    std::string join_path(const std::vector<std::string> &segments);
}

#endif
