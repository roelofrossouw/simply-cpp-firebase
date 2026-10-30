#include "sc_firebase.h"

#include <base64.h>
#include <rest.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace sc::firebase {
    namespace {
        // base64url without padding (RFC 7515), as JWTs want.
        std::string base64url(std::string_view bytes) {
            auto text = sc::base64::encode(bytes);
            std::ranges::replace(text, '+', '-');
            std::ranges::replace(text, '/', '_');
            while (!text.empty() && text.back() == '=') text.pop_back();
            return text;
        }

        // Percent-encodes all but RFC 3986's unreserved characters.
        std::string percent_encoded(std::string_view text) {
            static constexpr char hex[] = "0123456789ABCDEF";
            std::string out;
            for (const unsigned char c: text) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    out += static_cast<char>(c);
                } else {
                    out += '%';
                    out += hex[c >> 4];
                    out += hex[c & 15];
                }
            }
            return out;
        }

        // SHA-256 with RSA (RS256) over data, with the PEM private key.
        std::string rs256(const std::string &pem, std::string_view data) {
            const std::unique_ptr<BIO, decltype(&BIO_free)> bio{BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free};
            const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{
                bio ? PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr) : nullptr, EVP_PKEY_free};
            if (!key) throw std::invalid_argument("sc::firebase: the service account's private_key isn't a PEM private key");
            const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
            std::size_t length = 0;
            const auto *bytes = reinterpret_cast<const unsigned char *>(data.data());
            if (!context || EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1 ||
                EVP_DigestSign(context.get(), nullptr, &length, bytes, data.size()) != 1)
                throw std::invalid_argument("sc::firebase: can't sign with the service account's key");
            std::string signature(length, '\0');
            if (EVP_DigestSign(context.get(), reinterpret_cast<unsigned char *>(signature.data()), &length, bytes, data.size()) != 1)
                throw std::invalid_argument("sc::firebase: can't sign with the service account's key");
            signature.resize(length);
            return signature;
        }

        // Google's message in an error body: {"error": "..."} (the database), {"error": "invalid_grant",
        // "error_description": "..."} (tokens), {"error": {"message": "..."}} (other APIs); else the body.
        std::string message_of(const std::string &body) {
            const auto parsed = json::parse(body, nullptr, false);
            if (parsed.is_object() && parsed.contains("error")) {
                const auto &error = parsed["error"];
                if (error.is_string()) {
                    auto text = error.get<std::string>();
                    if (parsed.contains("error_description") && parsed["error_description"].is_string())
                        text += ": " + parsed["error_description"].get<std::string>();
                    return text;
                }
                if (error.is_object() && error.contains("message") && error["message"].is_string()) return error["message"].get<std::string>();
            }
            return body.empty() ? std::string{"no answer"} : body;
        }

        // The response as JSON, or an error for a status that isn't 2xx (or none).
        json answered(const sc::rest &call, const std::string &body, const std::string &what) {
            const auto status = call.status();
            if (status == 0) throw error(0, what + ": no answer: " + call.error());
            if (status < 200 || status >= 300) throw error(status, what + ": " + std::to_string(status) + " " + message_of(body));
            if (body.empty()) return nullptr;
            const auto parsed = json::parse(body, nullptr, false);
            if (parsed.is_discarded()) throw error(status, what + ": the answer isn't JSON");
            return parsed;
        }
    }

    // service_account

    service_account service_account::parse(const std::string_view text) {
        const auto document = json::parse(text, nullptr, false);
        if (!document.is_object()) throw std::invalid_argument("sc::firebase: a service account is a JSON object");
        const auto text_of = [&](const char *name) {
            return document.contains(name) && document[name].is_string() ? document[name].get<std::string>() : std::string{};
        };
        service_account account;
        account.project_id = text_of("project_id");
        account.client_email = text_of("client_email");
        account.private_key = text_of("private_key");
        account.private_key_id = text_of("private_key_id");
        if (const auto uri = text_of("token_uri"); !uri.empty()) account.token_uri = uri;
        if (account.client_email.empty() || account.private_key.empty())
            throw std::invalid_argument("sc::firebase: a service account needs client_email and private_key");
        return account;
    }

    service_account service_account::load(const std::filesystem::path &file) {
        std::ifstream in{file, std::ios::binary};
        if (!in) throw std::runtime_error("sc::firebase: can't read " + file.string());
        const std::string text{std::istreambuf_iterator<char>{in}, {}};
        return parse(text);
    }

    // credentials

    const std::vector<std::string> &credentials::default_scopes() {
        static const std::vector<std::string> scopes{"https://www.googleapis.com/auth/firebase.database",
                                                     "https://www.googleapis.com/auth/userinfo.email",
                                                     "https://www.googleapis.com/auth/cloud-platform"};
        return scopes;
    }

    credentials::credentials(service_account account, std::vector<std::string> scopes)
        : account_(std::move(account)), scopes_(std::move(scopes)) {
    }

    std::string credentials::assertion(const std::chrono::system_clock::time_point now) const {
        const auto issued = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        std::string scope;
        for (const auto &each: scopes_) scope += (scope.empty() ? "" : " ") + each;
        json header{{"alg", "RS256"}, {"typ", "JWT"}};
        if (!account_.private_key_id.empty()) header["kid"] = account_.private_key_id;
        const json claims{{"iss", account_.client_email}, {"scope", scope}, {"aud", account_.token_uri},
                          {"iat", issued}, {"exp", issued + 3600}};
        const auto signed_part = base64url(header.dump()) + "." + base64url(claims.dump());
        return signed_part + "." + base64url(rs256(account_.private_key, signed_part));
    }

    std::string credentials::access_token() {
        const std::lock_guard lock{mutex_};
        const auto now = std::chrono::system_clock::now();
        if (!token_.empty() && now < expires_ - std::chrono::minutes{5}) return token_;
        sc::rest call{account_.token_uri};
        call.quiet();
        call.header("Content-Type", "application/x-www-form-urlencoded");
        const auto body = call.timeout(30).post("grant_type=" + percent_encoded("urn:ietf:params:oauth:grant-type:jwt-bearer") +
                                                "&assertion=" + assertion(now));
        const auto answer = answered(call, body, "sc::firebase: access token");
        if (!answer.contains("access_token") || !answer["access_token"].is_string())
            throw error(call.status(), "sc::firebase: access token: no access_token in the answer");
        token_ = answer["access_token"].get<std::string>();
        expires_ = now + std::chrono::seconds{answer.value("expires_in", 3600)};
        return token_;
    }

    void credentials::forget() {
        const std::lock_guard lock{mutex_};
        token_.clear();
    }

    // paths

    std::vector<std::string> split_path(const std::string_view path) {
        std::vector<std::string> segments;
        std::size_t start = 0;
        while (start <= path.size()) {
            const auto slash = path.find('/', start);
            const auto end = slash == std::string_view::npos ? path.size() : slash;
            if (end > start) segments.emplace_back(path.substr(start, end - start));
            if (slash == std::string_view::npos) break;
            start = slash + 1;
        }
        return segments;
    }

    std::string join_path(const std::vector<std::string> &segments) {
        std::string out;
        for (const auto &each: segments) out += (out.empty() ? "" : "/") + each;
        return out;
    }

    // database

    database::database(std::string url, std::shared_ptr<credentials> access) : url_(std::move(url)), access_(std::move(access)) {
        while (!url_.empty() && url_.back() == '/') url_.pop_back();
        if (url_.empty()) throw std::invalid_argument("sc::firebase: a database needs its URL");
        if (!access_) throw std::invalid_argument("sc::firebase: a database needs credentials");
    }

    database::database(std::string url, service_account account)
        : database(std::move(url), std::make_shared<credentials>(std::move(account))) {
    }

    std::string database::url_of(const std::string_view path, const std::string_view query) const {
        std::string out = url_ + "/";
        bool first = true;
        for (const auto &segment: split_path(path)) {
            if (!first) out += '/';
            out += percent_encoded(segment);
            first = false;
        }
        out += ".json";
        if (!query.empty()) out += "?" + std::string{query};
        return out;
    }

    json database::request(const char *method, const std::string_view path, const json *body, const std::string_view query) const {
        const std::string what = std::string{"sc::firebase: "} + method + " /" + join_path(split_path(path));
        for (int attempt = 0;; ++attempt) {
            sc::rest call{url_of(path, query)};
            call.quiet();
            call.timeout(timeout_);
            call.bearer(access_->access_token());
            const std::string sent = body ? body->dump() : std::string{};
            std::string answer;
            const std::string_view verb = method;
            if (verb == "GET") answer = call.get();
            else if (verb == "PUT") answer = call.put(sent);
            else if (verb == "POST") answer = call.post(sent);
            else if (verb == "DELETE") answer = call.del();
            else {
                // sc::rest has no PATCH; the database takes a POST that says it's one.
                call.header("X-HTTP-Method-Override", method);
                answer = call.post(sent);
            }
            if (call.status() == 401 && attempt == 0) {
                access_->forget(); // the token may have been revoked or run out early: one more try
                continue;
            }
            return answered(call, answer, what);
        }
    }

    json database::get(const std::string_view path, const bool shallow) const {
        return request("GET", path, nullptr, shallow ? "shallow=true" : "");
    }

    std::vector<std::string> database::keys(const std::string_view path) const {
        std::vector<std::string> out;
        const auto value = get(path, true);
        if (value.is_object())
            for (const auto &[key, ignored]: value.items()) out.push_back(key);
        std::ranges::sort(out);
        return out;
    }

    void database::set(const std::string_view path, const json &value) const { request("PUT", path, &value); }

    void database::update(const std::string_view path, const json &values) const {
        if (!values.is_object()) throw std::invalid_argument("sc::firebase: update() takes an object of paths and values");
        if (values.empty()) return;
        request("PATCH", path, &values);
    }

    std::string database::push(const std::string_view path, const json &value) const {
        const auto answer = request("POST", path, &value);
        if (!answer.is_object() || !answer.contains("name") || !answer["name"].is_string())
            throw error(200, "sc::firebase: POST /" + join_path(split_path(path)) + ": no key in the answer");
        return answer["name"].get<std::string>();
    }

    void database::remove(const std::string_view path) const { request("DELETE", path, nullptr); }

    bool database::move(const std::string_view from, const std::string_view to, const bool overwrite) const {
        const auto source = split_path(from);
        const auto target = split_path(to);
        if (source == target) return !get(from).is_null();
        const auto inside = [](const std::vector<std::string> &outer, const std::vector<std::string> &inner) {
            return outer.size() <= inner.size() && std::equal(outer.begin(), outer.end(), inner.begin());
        };
        if (source.empty() || target.empty() || inside(source, target) || inside(target, source))
            throw std::invalid_argument("sc::firebase: move() can't move a path into itself, its parent or the root");
        auto value = get(from);
        if (value.is_null()) return false;
        if (!overwrite && !get(to).is_null()) return false;
        // The update goes to the deepest parent both share; its member names are the rest of each path.
        std::size_t common = 0;
        while (common < source.size() && common < target.size() && source[common] == target[common]) ++common;
        const std::vector<std::string> parent(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(common));
        const auto rest_of = [common](const std::vector<std::string> &segments) {
            return join_path({segments.begin() + static_cast<std::ptrdiff_t>(common), segments.end()});
        };
        json values = json::object();
        values[rest_of(source)] = nullptr;
        values[rest_of(target)] = std::move(value);
        update(join_path(parent), values);
        return true;
    }
}
