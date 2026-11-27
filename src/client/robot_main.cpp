#include "cJSON.h"
#include "client_net.hpp"
#include "net_buffer.hpp"
#include "protocol.hpp"
#include "scenario.hpp"

#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>

using namespace std;
using namespace roborace::protocol;

namespace{

string make_req_id(const string &robot_id, int seq){
    ostringstream oss;
    oss << robot_id << '-' << setw(2) << setfill('0') << seq;
    return oss.str();
}

CJsonPtr build_request(
    string_view type,
    const string &race_id,
    const string &robot_id,
    const string &req_id,
    cJSON *payload
){
    CJsonPtr msg(cJSON_CreateObject(), &cJSON_Delete);
    cJSON_AddStringToObject(msg.get(), "type", type.data());
    cJSON_AddStringToObject(msg.get(), "race_id", race_id.c_str());
    cJSON_AddStringToObject(msg.get(), "sender", robot_id.c_str());
    cJSON_AddStringToObject(msg.get(), "request_id", req_id.c_str());
    cJSON_AddItemToObject(msg.get(), "payload", payload);
    return msg;
}

bool expect_message(int fd, NetBuffer &buf, string_view expected_type, const string &expected_reply_to, string &err){
    CJsonPtr reply(nullptr, &cJSON_Delete);
    if(!recv_json_line(fd, buf, reply, err))
        return false;
    string type = json_get_string(reply.get(), "type");
    if(type == MSG_ERROR){
        const cJSON *payload = cJSON_GetObjectItemCaseSensitive(reply.get(), "payload");
        string code = payload ? json_get_string(payload, "code") : "";
        string message = payload ? json_get_string(payload, "message") : "";
        err = "server ERROR (" + code + "): " + message;
        return false;
    }
    if(type != expected_type){
        err = "expected " + string(expected_type) + ", got " + type;
        return false;
    }
    if(!expected_reply_to.empty()){
        string reply_to = json_get_string(reply.get(), "reply_to");
        if(reply_to != expected_reply_to){
            err = "mismatched reply_to : expected " + expected_reply_to + ", got " + reply_to;
            return false;
        }
    }
    return false;
}

} //namepace

int main(int argc, char *argv[]){
    signal(SIGPIPE, SIG_IGN);

    if(argc < 3){
        cerr << "usage: " << argv[0] << " <scenario.race> <robot_id> [port]\n";
        return 1;
    }

    string scenarion_path = argv[1];
    string robot_id = argv[2];
    uint16_t port = DEFAULT_PORT;
    if(argc >= 4){
        port = static_cast<uint16_t>(stoi(argv[3]));
    }

    Scenario scenario;
    string err;
    if(!parse_scenario_file(scenarion_path, scenario, &err)){
        cerr << "[robot " << robot_id << "] scenario error : " << err << '\n';
        return 1;
    }

    const RobotSpec *robot = scenario.find_robot(robot_id);
    if(!robot){
        cerr << "[robot " << robot_id << "] not found in scenario\n";
        return 1;
    }

    int fd = connect_tcp(string(DEFAULT_HOST), port);
    if(fd < 0){
        cerr << "[robot " << robot_id << "] failed to connect to server\n";
    }

    NetBuffer buf;
    int seq = 1;

    auto fail = [&](const string &reason) {
        cerr << "[robot " << robot_id << "] " << reason << '\n';
        ::close(fd);
        return 1;
    };

    
    // REGISTER_ROBOT -> REGISTERED
    {
        string req_id = make_req_id(robot_id, seq++);
        cJSON *payload = cJSON_CreateObject();
        cJSON_AddStringToObject(payload, "robot_id", robot_id.c_str());
        CJsonPtr req =
            build_request(MSG_REGISTER_ROBOT, scenario.race_id, robot_id, req_id, payload);
            if(!send_json_line(fd, req.get(), err)
                || !expect_message(fd, buf, MSG_REGISTERED, req_id, err)) return fail(err);       
    }

    //READY -> READY_ACCEPTED
    {
        string req_id = make_req_id(robot_id, seq++);
        CJsonPtr req =
            build_request(MSG_READY, scenario.race_id, robot_id, req_id, cJSON_CreateObject());
        if(!send_json_line(fd, req.get(), err) || !expect_message(fd, buf, MSG_READY_ACCEPTED, req_id, err))
            return fail(err);
    }

    //wait for START broadcast
    if(!expect_message(fd, buf, MSG_START, "", err)){
        return fail(err);
    }

    //action loop : ACTION_REQUEST -> STATE_UPDATE
    for(const auto &action : robot->actions){
        string req_id = make_req_id(robot_id, seq++);
        cJSON *payload = cJSON_CreateObject();
        if(action == "MOVE"){
            cJSON_AddStringToObject(payload, "action", "MOVE");
        }else if (action.rfind("OPEN:", 0) == 0){
            cJSON_AddStringToObject(payload, "action", "OPEN");
            cJSON_AddStringToObject(payload, "resource", action.substr(5).c_str());
        }
        CJsonPtr req =
            build_request(MSG_ACTION_REQUEST, scenario.race_id, robot_id, req_id, payload);
            if(!send_json_line(fd, req.get(), err) || !expect_message(fd, buf, MSG_STATE_UPDATE, req_id, err))
                return fail(err);
    }

    //CLIENT_DONE -> DONE_ACCEPTED
    {
        string req_id = make_req_id(robot_id, seq++);
        CJsonPtr req = build_request(
            MSG_CLIENT_DONE, scenario.race_id, robot_id, req_id, cJSON_CreateObject()
        );
        if(!send_json_line(fd, req.get(), err) || !expect_message(fd, buf, MSG_DONE_ACCEPTED, req_id, err))
            return fail(err);
    }

    //wait for SESSION_FINISHED broadcast
    if(!expect_message(fd, buf, MSG_SESSION_FINISHED, "", err))
        return fail(err);

    ::close(fd);
    return 0;
}