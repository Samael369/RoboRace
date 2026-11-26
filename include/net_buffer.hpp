#pragma once

#include <cstddef>
#include <string>

enum class LineStatus { OK, NEED_MORE, TOO_LONG };

class NetBuffer {
  public:
    void append(const char *data, std::size_t len);
    bool has_peding_data() const;
    void clear();
    LineStatus pop_line(std::string &out_line);

  private:
    static constexpr std::size_t MAX_LINE_LENGTH = 65536;

    // unconsumed bytes received so far from that socket
    std::string buffer_;
};
