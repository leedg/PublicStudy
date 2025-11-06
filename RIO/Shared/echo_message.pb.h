#pragma once

#include <cstdint>
#include <string>

namespace rio::echo {

class EchoMessage {
public:
    EchoMessage() = default;

    void set_text(const std::string& text) { text_ = text; }
    void set_text(std::string&& text) { text_ = std::move(text); }
    const std::string& text() const { return text_; }

    bool SerializeToString(std::string* out) const;
    bool ParseFromArray(const void* data, int size);
    int  ByteSizeLong() const;

private:
    std::string text_;
};

} // namespace rio::echo
