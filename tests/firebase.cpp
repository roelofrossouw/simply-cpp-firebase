// sc-firebase without the network: a service account's JSON, the signed token request (checked
// with the key it was signed with), and the database's paths and URLs.
#include "sc_firebase.h"
#include "sc_test.h"

#include <base64.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <chrono>
#include <memory>
#include <string>

namespace {
    // A new RSA key as PEM (PKCS#8), as a service account's private_key is.
    std::string new_key() {
        const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{EVP_RSA_gen(2048), EVP_PKEY_free};
        const std::unique_ptr<BIO, decltype(&BIO_free)> bio{BIO_new(BIO_s_mem()), BIO_free};
        PEM_write_bio_PrivateKey(bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr);
        char *data = nullptr;
        const auto length = BIO_get_mem_data(bio.get(), &data);
        return {data, static_cast<std::size_t>(length)};
    }

    std::string from_base64url(std::string text) {
        for (auto &c: text) c = c == '-' ? '+' : c == '_' ? '/' : c;
        while (text.size() % 4) text += '=';
        return sc::base64::decode(text);
    }

    bool verified(const std::string &pem, const std::string &data, const std::string &signature) {
        const std::unique_ptr<BIO, decltype(&BIO_free)> bio{BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free};
        const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr),
                                                                      EVP_PKEY_free};
        const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
        return EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr, key.get()) == 1 &&
               EVP_DigestVerify(context.get(), reinterpret_cast<const unsigned char *>(signature.data()), signature.size(),
                                reinterpret_cast<const unsigned char *>(data.data()), data.size()) == 1;
    }
}

int main() {
    const auto pem = new_key();
    const sc::firebase::json document{{"type", "service_account"},
                                      {"project_id", "example-app"},
                                      {"private_key_id", "abc123"},
                                      {"private_key", pem},
                                      {"client_email", "firebase-adminsdk@example-app.iam.gserviceaccount.com"},
                                      {"token_uri", "https://oauth2.googleapis.com/token"}};

    SECTION("A service account from its JSON");
    {
        const auto account = sc::firebase::service_account::parse(document.dump());
        CHECK_EQ(account.project_id, std::string{"example-app"});
        CHECK_EQ(account.client_email, std::string{"firebase-adminsdk@example-app.iam.gserviceaccount.com"});
        CHECK_EQ(account.private_key, pem);
        CHECK_EQ(account.private_key_id, std::string{"abc123"});
        auto without_uri = document;
        without_uri.erase("token_uri");
        CHECK_EQ(sc::firebase::service_account::parse(without_uri.dump()).token_uri, std::string{"https://oauth2.googleapis.com/token"});
        auto without_key = document;
        without_key.erase("private_key");
        CHECK_THROWS_AS((void) sc::firebase::service_account::parse(without_key.dump()), std::invalid_argument);
        CHECK_THROWS_AS((void) sc::firebase::service_account::parse("not json"), std::invalid_argument);
        CHECK_THROWS_AS((void) sc::firebase::service_account::load("/nonexistent/service-account.json"), std::runtime_error);
    }

    SECTION("The token request: a JWT signed with the account's key (RS256)");
    {
        const sc::firebase::credentials access{sc::firebase::service_account::parse(document.dump())};
        const auto now = std::chrono::system_clock::time_point{std::chrono::seconds{1793000000}};
        const auto jwt = access.assertion(now);
        const auto first = jwt.find('.');
        const auto second = jwt.find('.', first + 1);
        CHECK(first != std::string::npos && second != std::string::npos);
        CHECK(jwt.find_first_of("+/=") == std::string::npos); // base64url, unpadded
        const auto header = sc::firebase::json::parse(from_base64url(jwt.substr(0, first)));
        const auto claims = sc::firebase::json::parse(from_base64url(jwt.substr(first + 1, second - first - 1)));
        CHECK_EQ(header["alg"].get<std::string>(), std::string{"RS256"});
        CHECK_EQ(header["kid"].get<std::string>(), std::string{"abc123"});
        CHECK_EQ(claims["iss"].get<std::string>(), std::string{"firebase-adminsdk@example-app.iam.gserviceaccount.com"});
        CHECK_EQ(claims["aud"].get<std::string>(), std::string{"https://oauth2.googleapis.com/token"});
        CHECK_EQ(claims["iat"].get<long long>(), 1793000000LL);
        CHECK_EQ(claims["exp"].get<long long>(), 1793003600LL);
        CHECK(claims["scope"].get<std::string>().find("https://www.googleapis.com/auth/firebase.database") != std::string::npos);
        CHECK(claims["scope"].get<std::string>().find("https://www.googleapis.com/auth/userinfo.email") != std::string::npos);
        CHECK(verified(pem, jwt.substr(0, second), from_base64url(jwt.substr(second + 1))));
        CHECK(!verified(pem, jwt.substr(0, second) + "x", from_base64url(jwt.substr(second + 1))));

        const sc::firebase::credentials scoped{sc::firebase::service_account::parse(document.dump()), {"https://example.com/scope"}};
        const auto scoped_jwt = scoped.assertion(now);
        const auto scoped_first = scoped_jwt.find('.');
        const auto scoped_claims = sc::firebase::json::parse(
            from_base64url(scoped_jwt.substr(scoped_first + 1, scoped_jwt.find('.', scoped_first + 1) - scoped_first - 1)));
        CHECK_EQ(scoped_claims["scope"].get<std::string>(), std::string{"https://example.com/scope"});

        auto broken = sc::firebase::service_account::parse(document.dump());
        broken.private_key = "-----BEGIN PRIVATE KEY-----\nnot a key\n-----END PRIVATE KEY-----\n";
        CHECK_THROWS_AS((void) sc::firebase::credentials{broken}.assertion(now), std::invalid_argument);
    }

    SECTION("Paths and URLs");
    {
        using sc::firebase::split_path;
        CHECK(split_path("/devices//42/") == (std::vector<std::string>{"devices", "42"}));
        CHECK(split_path("").empty() && split_path("/").empty());
        CHECK_EQ(sc::firebase::join_path({"a", "b", "c"}), std::string{"a/b/c"});

        const sc::firebase::database db{"https://example-app.firebaseio.com/", sc::firebase::service_account::parse(document.dump())};
        CHECK_EQ(db.url(), std::string{"https://example-app.firebaseio.com"});
        CHECK_EQ(db.url_of(""), std::string{"https://example-app.firebaseio.com/.json"});
        CHECK_EQ(db.url_of("/devices/42/"), std::string{"https://example-app.firebaseio.com/devices/42.json"});
        CHECK_EQ(db.url_of("a b/ć", "shallow=true"), std::string{"https://example-app.firebaseio.com/a%20b/%C4%87.json?shallow=true"});
        CHECK_THROWS_AS((sc::firebase::database{"", sc::firebase::service_account::parse(document.dump())}), std::invalid_argument);
        CHECK_THROWS_AS(db.update("x", sc::firebase::json::array()), std::invalid_argument);
        CHECK_THROWS_AS((void) db.move("a", "a/b"), std::invalid_argument);   // into itself
        CHECK_THROWS_AS((void) db.move("a/b", "a"), std::invalid_argument);   // onto its parent
        CHECK_THROWS_AS((void) db.move("", "b"), std::invalid_argument);      // the root
    }

    SECTION("An error carries the status");
    {
        const sc::firebase::error refused{401, "Permission denied"};
        CHECK_EQ(refused.status(), 401L);
        CHECK_EQ(std::string{refused.what()}, std::string{"Permission denied"});
    }

    TEST_SUMMARY();
}
