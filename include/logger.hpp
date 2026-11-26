#pragma once

#include <fstream>
#include <mutex>
#include <string>
class Logger {
  public:
    Logger() = default;
    ~Logger();
    bool open(const std::string &path = "events.log");
    void close();
    void
    log(int tick,
        const std::string &actor,
        const std::string &event,
        const std::string &resource,
        const std::string &result,
        const std::string &req);

  private:
    std::ofstream file_;
    std::mutex mux_;

    static std::string_view normalize(std::string_view value) {
        return value.empty() ? "-" : value;
    }
};
