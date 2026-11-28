#include "cJSON.h"
#include "client_net.hpp"
#include "net_buffer.hpp"
#include "protocol.hpp"
#include "scenario.hpp"

#include <csignal>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>

using namespace std;
using namespace roborace::protocol;

namespace {

void notify_launcher(int &notify_fd, const string &msg) {
    if (notify_fd < 0)
        return;

    ssize_t ignored = ::write(notify_fd, msg.data(), msg.size());
    (void)ignored;
    ::close(notify_fd);
    notify_fd = -1;
}

bool expect_message(
    int fd, NetBuffer &buf, string_view expected_type, const string &expected_reply_to, string &err
) {
    CJsonPtr reply(nullptr, &cJSON_Delete);
    if (!recv_json_line(fd, buf, reply, err))
        return false;

    string type = json_get_string(reply.get(), "type");
    if (type == MSG_ERROR) {
        const cJSON *payload = cJSON_GetObjectItemCaseSensitive(reply.get(), "payload");
        string code = payload ? json_get_string(payload, "code") : "";
        string message = payload ? json_get_string(payload, "message") : "";
        err = "server ERROR (" + code + "): " + message;
        return false;
    }
    if (type != expected_type) {
        err = "expected " + string(expected_type) + ", got " + type;
        return false;
    }
    if (!expected_reply_to.empty()) {
        string reply_to = json_get_string(reply.get(), "reply_to");
        if (reply_to != expected_reply_to) {
            err = "mismatched reply_to: expected " + expected_reply_to + ", got " + reply_to;
            return false;
        }
    }
    return true;
}
} // namespace

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2) {
        cerr << "usage: " << argv[0] << " <scenario.race> [port] [notify_fd]\n";
        return 1;
    }

    string scenario_path = argv[1];
    uint16_t port = DEFAULT_PORT;
    int notify_fd = -1;
    if (argc >= 3) {
        port = static_cast<uint16_t>(stoi(argv[2]));
    }
    if (argc >= 4) {
        notify_fd = stoi(argv[3]);
    }

    ifstream file(scenario_path);
    if (!file.is_open()) {
        cerr << "[controller] failed to open scenario: " << scenario_path << '\n';
        notify_launcher(notify_fd, "FAILED\n");
        return 1;
    }
    ostringstream oss;
    oss << file.rdbuf();
    string raw = oss.str();

    CJsonPtr scenario_json(cJSON_Parse(raw.c_str()), &cJSON_Delete);
    Scenario scenario;
    string err;
    if (!scenario_json || !parse_scenario_json(raw, scenario, &err)) {
        cerr << "[controller] invalid scenario: " << (err.empty() ? "JSON parse error: " : err)
             << '\n';
        notify_launcher(notify_fd, "FAILED\n");
        return 1;
    }

    int fd = connect_tcp(string(DEFAULT_HOST), port);
    if (fd < 0) {
        cerr << "[controller] failed to connect to server\n";
        notify_launcher(notify_fd, "FAILED\n");
        return 1;
    }

    NetBuffer buf;
    const string req_id = "CTRL-01";

    // SCENARIO_UPLOAD -> SCENARIO_ACCEPTED
    {
        CJsonPtr req(cJSON_CreateObject(), &cJSON_Delete);
        cJSON_AddStringToObject(req.get(), "type", MSG_SCENARIO_UPLOAD.data());
        cJSON_AddStringToObject(req.get(), "race_id", scenario.race_id.c_str());
        cJSON_AddStringToObject(req.get(), "sender", ACTOR_CONTROLLER.data());
        cJSON_AddStringToObject(req.get(), "request_id", req_id.c_str());

        cJSON *payload = cJSON_CreateObject();
        cJSON_AddItemToObject(payload, "scenario", scenario_json.release());
        cJSON_AddItemToObject(req.get(), "payload", payload);

        if (!send_json_line(fd, req.get(), err) ||
            !expect_message(fd, buf, MSG_SCENARIO_ACCEPTED, req_id, err)) {
            cerr << "[controller] " << err << '\n';
            notify_launcher(notify_fd, "FAILED\n");
            ::close(fd);
            return 1;
        }
    }

    // tell launcher that scenario was accepted so it can spawn robots
    notify_launcher(notify_fd, "ACCEPTED\n");

    // wait for START, then SESSION_FINISHED
    if (!expect_message(fd, buf, MSG_START, "", err) ||
        !expect_message(fd, buf, MSG_SESSION_FINISHED, "", err)) {
        cerr << "[controller] " << err << '\n';
        ::close(fd);
        return 1;
    }

    ::close(fd);
    return 0;
}