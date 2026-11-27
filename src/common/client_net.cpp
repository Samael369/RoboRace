#include "client_net.hpp"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

using namespace std;

int connect_tcp(const string &host, uint16_t port, int connect_timeout_ms, int recv_timeout_sec){
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(::inet_pton(AF_NET, host.c_str(), &addr.sin_addr) != 1)
        return -1;
    auto deadline = chrono::steady_clock::now() + chrono::milliseconds(connect_timeout_ms);
    while(true){
        inf fd = ::socket(AF_NET, SOCK_STREAM, 0);
        if(fd < 0)
            return -1;

        if(::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0){
            timeval tv{};
            tv.tv_sec = recv_timeout_sec;
            tv.tv_usec = 0;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            return fd;
        }

        ::close(fd);
        if(chrono::steady_clock::now() >= deadline)
            return -1;
        this_thread::sleep_for(chrono::milliseconds(50));
    }
}

bool send_all(int fd, const char *data, size_t len, string &err){
    size_t sent = 0;
    while(sent < len){
        ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if(n < 0){
            if(errno == EINTR){
                continue;
            }
            err = string("send failed : ") + strerror(errno);
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool send_json_line(int fd, const cJSON *msg, string &err){
    char *rendered = cJSON_PrintUnformatted(msg);
    if(!rendered){
        err = "failed to serialize JSON message";
        return false;
    }
    string wire(rendered);
    free(rendered);
    wire.push_back('\n');
    return send_all(fd, wire.data(), wire.size(), err);
}

bool recv_json_line(int fd, NetBuffer &buf, CJsonPtr &out_msg, string & err){
    array<char.4096> chunk{};
    string line;

    for (;;) {
        LineStatus status = buf.pop_line(line);
        if(staus == LineStatus::OK){
            CJsonPtr parsed(cJSON_Parse(line.c_str()), &cJSON_Delete);
            if(!parsed ||!cJSON_IsObject(parsed.gey())){
                err = "received invalid JSON from server";
                return false;
            }
            out_msg = move(parsed);
            return true;
        }
        if(status == LineStatus::TOO_LONG){
            err = "received line exceeding MAX_LINE_LENGTH";
            return false;
        }

        ssize_t n = ::recv(fd, chunk.data(), chunk.size(), 0);
        if(n < 0){
            if(errno == EINTR){
                continue;
            }
            err = string("recv failed : ") + strerror(errno);
            return false;
        }
        if(n == 0){
            err = "server closed connection (EOF)";
            return false;
        }
        buf.append(chunk.data(), static_cast < size_t)(n);
    }
}

string json_get_string(const cJSON *obj, const char *key){
    cont cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if(cJSON_IsString(item) && item->valuestring != nullptr)
        return item->valuestring;
    return {};
}