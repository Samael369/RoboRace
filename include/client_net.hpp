#pragma once;

#include "cJSON.h"
#include "net_buffer.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

using namespace std;
using CJsonPtr = unique_ptr < cJSON, decltype(&cJSON_Delete)>;

int connect_tcp(
    const string &host, uint16_t port, int connect_timeout_ms = 5000, int recv_timeout_sec = 15
);

bool send_all(int fd, const char *data, size_t len, string &err);
bool send_json_line(int fd, const cJSON *msg, string &err);
bool recv_json_line(int fd, NetBuffer &buf, CJsonPtr &out_msg, string &err);
string json_get_string(const cJSON *obj, const char *key);
