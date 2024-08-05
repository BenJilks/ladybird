/*
 * Copyright (c) 2024, Ben Jilks <benjyjilks@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Error.h>
#include <AK/Utf8View.h>
#include <LibTextCodec/Decoder.h>
#include <LibTextCodec/Encoder.h>
#include <LibTextCodec/LookupTables.h>

namespace TextCodec {

namespace {
UTF8Encoder s_utf8_encoder;
EUCJPEncoder s_euc_jp_encoder;
}

Optional<Encoder&> encoder_for_exact_name(StringView encoding)
{
    if (encoding.equals_ignoring_ascii_case("utf-8"sv))
        return s_utf8_encoder;
    if (encoding.equals_ignoring_ascii_case("euc-jp"sv))
        return s_euc_jp_encoder;
    dbgln("TextCodec: No encoder implemented for encoding '{}'", encoding);
    return {};
}

Optional<Encoder&> encoder_for(StringView label)
{
    auto encoding = get_standardized_encoding(label);
    return encoding.has_value() ? encoder_for_exact_name(encoding.value()) : Optional<Encoder&> {};
}

// https://encoding.spec.whatwg.org/#utf-8-encoder
ErrorOr<void> UTF8Encoder::process(Utf8View input, Function<ErrorOr<void>(u8)> on_byte)
{
    for (auto item : input) {
        // 1. If code point is end-of-queue, return finished.

        // 2. If code point is an ASCII code point, return a byte whose value is code point.
        if (item < 0x0080) {
            TRY(on_byte(item));
            continue;
        }

        // 3. Set count and offset based on the range code point is in:
        size_t count = 0;
        size_t offset = 0;
        if (item >= 0x0080 && item <= 0x07FF) {
            count = 1;
            offset = 0xC0;
        } else if (item >= 0x0800 && item <= 0xFFFF) {
            count = 2;
            offset = 0xE0;
        } else if (item >= 0x10000 && item <= 0x10FFFF) {
            count = 3;
            offset = 0xF0;
        } else {
            // TODO: Report error.
            continue;
        }

        // 4. Let bytes be a byte sequence whose first byte is (code point >> (6 × count)) + offset.
        TRY(on_byte(static_cast<u8>((item >> (6 * count)) + offset)));

        // 5. While count is greater than 0:
        while (count > 0) {
            // 1. Set temp to code point >> (6 × (count − 1)).
            auto temp = item >> (6 * (count - 1));

            // 2. Append to bytes 0x80 | (temp & 0x3F).
            TRY(on_byte(static_cast<u8>(0x80 | (temp & 0x3F))));

            // 3. Decrease count by one.
            count -= 1;
        }

        // 6. Return bytes bytes, in order.
    }

    return {};
}

// https://encoding.spec.whatwg.org/#euc-jp-encoder
ErrorOr<void> EUCJPEncoder::process(Utf8View input, Function<ErrorOr<void>(u8)> on_byte)
{
    for (auto item : input) {
        // 1. If code point is end-of-queue, return finished.

        // 2. If code point is an ASCII code point, return a byte whose value is code point.
        if (item < 0x0080) {
            TRY(on_byte(static_cast<u8>(item)));
            continue;
        }

        // 3. If code point is U+00A5, return byte 0x5C.
        if (item == 0x00A5) {
            TRY(on_byte(static_cast<u8>(0x5C)));
            continue;
        }

        // 4. If code point is U+203E, return byte 0x7E.
        if (item == 0x203E) {
            TRY(on_byte(static_cast<u8>(0x7E)));
            continue;
        }

        // 5. If code point is in the range U+FF61 to U+FF9F, inclusive, return two bytes whose values are 0x8E and code point − 0xFF61 + 0xA1.
        if (item >= 0xFF61 && item <= 0xFF9F) {
            TRY(on_byte(0x8E));
            TRY(on_byte(static_cast<u8>(item - 0xFF61 + 0xA1)));
            continue;
        }

        // 6. If code point is U+2212, set it to U+FF0D.
        if (item == 0x2212)
            item = 0xFF0D;

        // 7. Let pointer be the index pointer for code point in index jis0208.
        auto pointer = code_point_jis0208_index(item);

        // 8. If pointer is null, return error with code point.
        if (!pointer.has_value()) {
            // TODO: Report error.
            continue;
        }

        // 9. Let lead be pointer / 94 + 0xA1.
        auto lead = *pointer / 94 + 0xA1;

        // 10. Let trail be pointer % 94 + 0xA1.
        auto trail = *pointer % 94 + 0xA1;

        // 11. Return two bytes whose values are lead and trail.
        TRY(on_byte(static_cast<u8>(lead)));
        TRY(on_byte(static_cast<u8>(trail)));
    }

    return {};
}

}
