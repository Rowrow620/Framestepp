#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace framestepp::detail {

struct Utf8Unit final {
    std::size_t width;
    bool valid;
};

[[nodiscard]] inline bool is_continuation_byte(const unsigned char byte) noexcept {
    return (byte & 0xC0U) == 0x80U;
}

[[nodiscard]] inline Utf8Unit decode_utf8_unit(const std::string_view text,
                                               const std::size_t offset) noexcept {
    if (offset >= text.size()) {
        return Utf8Unit{0, false};
    }

    const auto byte = [&text](const std::size_t index) {
        return static_cast<unsigned char>(text[index]);
    };
    const auto has_continuation = [&text, &byte](const std::size_t index) {
        return index < text.size() && is_continuation_byte(byte(index));
    };

    const auto first = byte(offset);
    if (first <= 0x7FU) {
        return Utf8Unit{1, true};
    }
    if (first >= 0xC2U && first <= 0xDFU && has_continuation(offset + 1)) {
        return Utf8Unit{2, true};
    }
    if (first == 0xE0U && offset + 2 < text.size() && byte(offset + 1) >= 0xA0U &&
        byte(offset + 1) <= 0xBFU && has_continuation(offset + 2)) {
        return Utf8Unit{3, true};
    }
    if (((first >= 0xE1U && first <= 0xECU) || (first >= 0xEEU && first <= 0xEFU)) &&
        has_continuation(offset + 1) && has_continuation(offset + 2)) {
        return Utf8Unit{3, true};
    }
    if (first == 0xEDU && offset + 2 < text.size() && byte(offset + 1) >= 0x80U &&
        byte(offset + 1) <= 0x9FU && has_continuation(offset + 2)) {
        return Utf8Unit{3, true};
    }
    if (first == 0xF0U && offset + 3 < text.size() && byte(offset + 1) >= 0x90U &&
        byte(offset + 1) <= 0xBFU && has_continuation(offset + 2) && has_continuation(offset + 3)) {
        return Utf8Unit{4, true};
    }
    if (first >= 0xF1U && first <= 0xF3U && has_continuation(offset + 1) &&
        has_continuation(offset + 2) && has_continuation(offset + 3)) {
        return Utf8Unit{4, true};
    }
    if (first == 0xF4U && offset + 3 < text.size() && byte(offset + 1) >= 0x80U &&
        byte(offset + 1) <= 0x8FU && has_continuation(offset + 2) && has_continuation(offset + 3)) {
        return Utf8Unit{4, true};
    }

    // Invalid and truncated sequences advance one byte so diagnostics can still
    // identify their exact location. The lexer will report the encoding error.
    return Utf8Unit{1, false};
}

[[nodiscard]] inline std::size_t utf8_boundary_before_or_at(const std::string_view text,
                                                            std::size_t offset) noexcept {
    offset = std::min(offset, text.size());
    std::size_t index = 0;
    while (index < offset) {
        const auto unit = decode_utf8_unit(text, index);
        const auto next = index + unit.width;
        if (next > offset) {
            return index;
        }
        index = next;
    }
    return offset;
}

[[nodiscard]] inline std::size_t utf8_boundary_after_or_at(const std::string_view text,
                                                           std::size_t offset) noexcept {
    offset = std::min(offset, text.size());
    std::size_t index = 0;
    while (index < offset) {
        const auto unit = decode_utf8_unit(text, index);
        const auto next = index + unit.width;
        if (next > offset) {
            return next;
        }
        index = next;
    }
    return offset;
}

} // namespace framestepp::detail
