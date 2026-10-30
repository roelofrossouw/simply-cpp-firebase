set(SC_TEST_LINK_LIBRARIES sc-firebase OpenSSL::Crypto)

add_sc_test(firebase LABELS unit)
# A real database: needs SC_FIREBASE_TEST_CREDENTIALS (a service account's JSON file) and
# SC_FIREBASE_TEST_URL (the database's URL); writes under SC_FIREBASE_TEST_ROOT (default
# "sc-firebase-test") and removes what it wrote. Skipped without them.
add_sc_test(firebase-live LABELS network)
