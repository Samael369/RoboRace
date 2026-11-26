#include "logger.hpp"
#include <ios>
#include <mutex>

bool Logger::open(const std::string &path) {
    std::lock_guard<std::mutex> lock(mux_);

    if (file_.is_open()) {
        file_.close();
    }

    file_.open(path, std::ios::out | std::ios::trunc);

    return file_.is_open();
}

void Logger::close() {
    std::lock_guard<std::mutex> lock(mux_);

    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

Logger::~Logger() {
    close();
}

void Logger::log(
    int tick,
    const std::string &actor,
    const std::string &event,
    const std::string &resource,
    const std::string &result,
    const std::string &req
) {
    std::lock_guard<std::mutex> lock(mux_);

    if (!file_.is_open()) {
        return;
    }

    file_ << "tick=" << tick << " actor=" << normalize(actor) << " event=" << normalize(event)
          << " resource=" << resource << " result=" << result << " req=" << normalize(req) << "\n";
    // Flush immediately to disk so crashes never lose data
    file_.flush();
}
