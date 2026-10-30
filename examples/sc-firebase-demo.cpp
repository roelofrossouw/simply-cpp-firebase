// A Firebase Realtime Database from the command line: get, keys, set, update, push, remove and move
// a path, signed in with a service account's key.
//
//     sc-firebase-demo -c service-account.json -u https://example-app.firebaseio.com get devices/42
//     sc-firebase-demo -c service-account.json -u https://example-app.firebaseio.com set devices/42 '{"online": 1}'
//     sc-firebase-demo -c service-account.json -u https://example-app.firebaseio.com move devices/42 archive/42

#include <params.h>
#include <sc_firebase.h>

#include <iostream>

int main(int argc, char **argv) {
    const sc::params args{argc, argv, {
        {"name", "sc-firebase-demo"},
        {"description", "Reads and writes a Firebase Realtime Database: get (the value, as JSON), keys (a node's child "
                        "keys), set, update (several paths at once; null removes), push (a new child; its key), remove, "
                        "and move (to another path in one update, not over what's there unless --overwrite)."},
        {"options", {
            {"credentials", {{"short", "c"}, {"value", "FILE"}, {"help", "the service account's JSON key, as the Firebase console downloads it"}}},
            {"url", {{"short", "u"}, {"value", "URL"}, {"help", "the database, https://<name>.firebaseio.com"}}},
            {"overwrite", {{"type", "boolean"}, {"help", "move: replace what's at the target"}}}}},
        {"positional", {
            {"command", {{"help", "get, keys, set, update, push, remove or move"}}},
            {"path", {{"help", "the path, '/'-separated"}}},
            {"value", {{"required", false}, {"help", "set, update, push: the JSON; move: the path to move to"}}}}}}};
    try {
        // [readme]
        const sc::firebase::database db{args.as<std::string>("url"),
                                        sc::firebase::service_account::load(args.as<std::string>("credentials"))};
        const auto command = args.as<std::string>("command");
        const auto path = args.as<std::string>("path");
        const auto value = args.as<std::string>("value", "");
        const auto json_value = [&] { return sc::firebase::json::parse(value.empty() ? "null" : value); };
        if (command == "get") std::cout << db.get(path).dump(2) << '\n';
        else if (command == "keys") for (const auto &key: db.keys(path)) std::cout << key << '\n';
        else if (command == "set") db.set(path, json_value());
        else if (command == "update") db.update(path, json_value());
        else if (command == "push") std::cout << db.push(path, json_value()) << '\n';
        else if (command == "remove") db.remove(path);
        else if (command == "move") {
            if (!db.move(path, value, args.as<bool>("overwrite"))) {
                std::cerr << "sc-firebase-demo: not moved: nothing at " << path << ", or something at " << value << " already\n";
                return 1;
            }
        } else {
            std::cerr << "sc-firebase-demo: unknown command " << command << '\n';
            return 2;
        }
        // [/readme]
    } catch (const std::exception &error) {
        std::cerr << "sc-firebase-demo: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
