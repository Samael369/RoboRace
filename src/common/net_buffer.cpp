#include "net_buffer.hpp"
#include "protocol.hpp"
#include <cstddef>
#include <string>

void NetBuffer::append(const char *data, std::size_t len) {
    buffer_.append(data, len);
}

LineStatus NetBuffer::pop_line(std::string &out_line) {
    using namespace roborace::protocol;

    std::size_t newline_position = buffer_.find('\n');

    if (newline_position > MAX_LINE_LENGTH) {
        return LineStatus::TOO_LONG;
    }

    if (newline_position == std::string::npos) {
        if (buffer_.size() > MAX_LINE_LENGTH) {
            return LineStatus::TOO_LONG;
        } else {
            return LineStatus::NEED_MORE;
        }
    }

    out_line = buffer_.substr(0, newline_position);
    buffer_.erase(0, newline_position + 1);

    if (!out_line.empty() && out_line.back() == '\r') {
        out_line.pop_back();
    }

    return LineStatus::OK;
}

bool NetBuffer::has_peding_data() const {
    return !buffer_.empty();
}

void NetBuffer::clear() {
    buffer_.clear();
}
