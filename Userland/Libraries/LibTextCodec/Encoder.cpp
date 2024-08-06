/*
 * Copyright (c) 2024, Ben Jilks <benjyjilks@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/BinarySearch.h>
#include <AK/Error.h>
#include <AK/Utf8View.h>
#include <LibTextCodec/Decoder.h>
#include <LibTextCodec/Encoder.h>
#include <LibTextCodec/LookupTables.h>

namespace TextCodec {

namespace {
UTF8Encoder s_utf8_encoder;
GB18030Encoder s_gb18030_encoder;
GB18030Encoder s_gbk_encoder(GB18030Encoder::IsGBK::Yes);
Big5Encoder s_big5_encoder;
EUCJPEncoder s_euc_jp_encoder;
ISO2022JPEncoder s_iso_2022_jp_encoder;
ShiftJISEncoder s_shift_jis_encoder;
EUCKREncoder s_euc_kr_encoder;
}

Optional<Encoder&> encoder_for_exact_name(StringView encoding)
{
    if (encoding.equals_ignoring_ascii_case("utf-8"sv))
        return s_utf8_encoder;
    if (encoding.equals_ignoring_ascii_case("big5"sv))
        return s_big5_encoder;
    if (encoding.equals_ignoring_ascii_case("euc-jp"sv))
        return s_euc_jp_encoder;
    if (encoding.equals_ignoring_ascii_case("iso-2022-jp"sv))
        return s_iso_2022_jp_encoder;
    if (encoding.equals_ignoring_ascii_case("shift_jis"sv))
        return s_shift_jis_encoder;
    if (encoding.equals_ignoring_ascii_case("euc-kr"sv))
        return s_euc_kr_encoder;
    if (encoding.equals_ignoring_ascii_case("gb18030"sv))
        return s_gb18030_encoder;
    if (encoding.equals_ignoring_ascii_case("gbk"sv))
        return s_gbk_encoder;
    dbgln("TextCodec: No encoder implemented for encoding '{}'", encoding);
    return {};
}

Optional<Encoder&> encoder_for(StringView label)
{
    auto encoding = get_standardized_encoding(label);
    return encoding.has_value() ? encoder_for_exact_name(encoding.value()) : Optional<Encoder&> {};
}

// https://encoding.spec.whatwg.org/#utf-8-encoder
ErrorOr<void> UTF8Encoder::process(Utf8View input, ErrorMode, Function<ErrorOr<void>(u8, AlwaysEscape)> on_byte)
{
    for (auto item : input) {
        // 1. If code point is end-of-queue, return finished.

        // 2. If code point is an ASCII code point, return a byte whose value is code point.
        if (item < 0x0080) {
            TRY(on_byte(item, AlwaysEscape::No));
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
        }

        // 4. Let bytes be a byte sequence whose first byte is (code point >> (6 × count)) + offset.
        TRY(on_byte(static_cast<u8>((item >> (6 * count)) + offset), AlwaysEscape::No));

        // 5. While count is greater than 0:
        while (count > 0) {
            // 1. Set temp to code point >> (6 × (count − 1)).
            auto temp = item >> (6 * (count - 1));

            // 2. Append to bytes 0x80 | (temp & 0x3F).
            TRY(on_byte(static_cast<u8>(0x80 | (temp & 0x3F)), AlwaysEscape::No));

            // 3. Decrease count by one.
            count -= 1;
        }

        // 6. Return bytes bytes, in order.
    }

    return {};
}

// https://encoding.spec.whatwg.org/#concept-encoding-process
static ErrorOr<void> handle_error(Encoder::ErrorMode error_mode, u32 code_point, Function<ErrorOr<void>(u8, Encoder::AlwaysEscape)>& on_byte)
{
    // 7. Otherwise, if result is an error, switch on mode and run the associated steps:
    switch (error_mode) {
    case Encoder::ErrorMode::Replacement:
        // Push U+FFFD (�) to output.
        TRY(on_byte(0xFF, Encoder::AlwaysEscape::Yes));
        TRY(on_byte(0xFD, Encoder::AlwaysEscape::Yes));
        return {};
    case Encoder::ErrorMode::Html: {
        // Push 0x26 (&), 0x23 (#), followed by the shortest sequence of 0x30 (0) to 0x39 (9), inclusive, representing
        // result’s code point’s value in base ten, followed by 0x3B (;) to output.
        TRY(on_byte(0x26, Encoder::AlwaysEscape::Yes));
        TRY(on_byte(0x23, Encoder::AlwaysEscape::Yes));

        Vector<u8> digits;
        for (u32 next_digits = code_point; next_digits > 0; next_digits /= 10)
            digits.append(0x30 + (next_digits % 10));
        for (u8 digit : digits.in_reverse())
            TRY(on_byte(digit, Encoder::AlwaysEscape::No));

        TRY(on_byte(0x3B, Encoder::AlwaysEscape::Yes));
        return {};
    }
    case Encoder::ErrorMode::Fatal:
        // Return result.
        return Error::from_string_view("Fatal encoding error"sv);
    }

    return {};
}

// https://encoding.spec.whatwg.org/#euc-jp-encoder
ErrorOr<void> EUCJPEncoder::process(Utf8View input, ErrorMode error_mode, Function<ErrorOr<void>(u8, AlwaysEscape)> on_byte)
{
    for (auto item : input) {
        // 1. If code point is end-of-queue, return finished.

        // 2. If code point is an ASCII code point, return a byte whose value is code point.
        if (item < 0x0080) {
            TRY(on_byte(static_cast<u8>(item), AlwaysEscape::No));
            continue;
        }

        // 3. If code point is U+00A5, return byte 0x5C.
        if (item == 0x00A5) {
            TRY(on_byte(static_cast<u8>(0x5C), AlwaysEscape::No));
            continue;
        }

        // 4. If code point is U+203E, return byte 0x7E.
        if (item == 0x203E) {
            TRY(on_byte(static_cast<u8>(0x7E), AlwaysEscape::No));
            continue;
        }

        // 5. If code point is in the range U+FF61 to U+FF9F, inclusive, return two bytes whose values are 0x8E and code point − 0xFF61 + 0xA1.
        if (item >= 0xFF61 && item <= 0xFF9F) {
            TRY(on_byte(0x8E, AlwaysEscape::No));
            TRY(on_byte(static_cast<u8>(item - 0xFF61 + 0xA1), AlwaysEscape::No));
            continue;
        }

        // 6. If code point is U+2212, set it to U+FF0D.
        if (item == 0x2212)
            item = 0xFF0D;

        // 7. Let pointer be the index pointer for code point in index jis0208.
        auto pointer = code_point_jis0208_index(item);

        // 8. If pointer is null, return error with code point.
        if (!pointer.has_value()) {
            TRY(handle_error(error_mode, item, on_byte));
            continue;
        }

        // 9. Let lead be pointer / 94 + 0xA1.
        auto lead = *pointer / 94 + 0xA1;

        // 10. Let trail be pointer % 94 + 0xA1.
        auto trail = *pointer % 94 + 0xA1;

        // 11. Return two bytes whose values are lead and trail.
        TRY(on_byte(static_cast<u8>(lead), AlwaysEscape::No));
        TRY(on_byte(static_cast<u8>(trail), AlwaysEscape::No));
    }

    return {};
}

// https://encoding.spec.whatwg.org/#iso-2022-jp-encoder
ErrorOr<ISO2022JPEncoder::State> ISO2022JPEncoder::process_item(u32 item, State state, ErrorMode error_mode, Function<ErrorOr<void>(u8, AlwaysEscape)>& on_byte)
{
    // 3. If ISO-2022-JP encoder state is ASCII or Roman, and code point is U+000E, U+000F, or U+001B, return error with U+FFFD.
    if (state == State::ASCII || state == State::Roman) {
        if (item == 0x000E || item == 0x000F || item == 0x001B) {
            TRY(handle_error(error_mode, 0xFFFD, on_byte));
            return state;
        }
    }

    // 4. If ISO-2022-JP encoder state is ASCII and code point is an ASCII code point, return a byte whose value is code point.
    if (state == State::ASCII && item < 0x0080) {
        TRY(on_byte(static_cast<u8>(item), AlwaysEscape::No));
        return state;
    }

    // 5. If ISO-2022-JP encoder state is Roman and code point is an ASCII code point, excluding U+005C and U+007E, or is U+00A5 or U+203E, then:
    if (state == State::Roman && ((item < 0x0080 && item != 0x005C && item != 0x007E) || (item == 0x00A5 || item == 0x203E))) {
        // 1. If code point is an ASCII code point, return a byte whose value is code point.
        if (item < 0x0080) {
            TRY(on_byte(static_cast<u8>(item), AlwaysEscape::No));
            return state;
        }

        // 2. If code point is U+00A5, return byte 0x5C.
        if (item == 0x00A5) {
            TRY(on_byte(0x5C, AlwaysEscape::No));
            return state;
        }

        // 3. If code point is U+203E, return byte 0x7E.
        if (item == 0x203E) {
            TRY(on_byte(0x7E, AlwaysEscape::No));
            return state;
        }
    }

    // 6. If code point is an ASCII code point, and ISO-2022-JP encoder state is not ASCII, restore code point to ioQueue, set
    //    ISO-2022-JP encoder state to ASCII, and return three bytes 0x1B 0x28 0x42.
    if (item < 0x0080 && state != State::ASCII) {
        TRY(on_byte(0x1B, AlwaysEscape::No));
        TRY(on_byte(0x28, AlwaysEscape::No));
        TRY(on_byte(0x42, AlwaysEscape::No));
        return process_item(item, State::ASCII, error_mode, on_byte);
    }

    // 7. If code point is either U+00A5 or U+203E, and ISO-2022-JP encoder state is not Roman, restore code point to ioQueue,
    //    set ISO-2022-JP encoder state to Roman, and return three bytes 0x1B 0x28 0x4A.
    if ((item == 0x00A5 || item == 0x203E) && state != State::Roman) {
        TRY(on_byte(0x1B, AlwaysEscape::No));
        TRY(on_byte(0x28, AlwaysEscape::No));
        TRY(on_byte(0x4A, AlwaysEscape::No));
        return process_item(item, State::Roman, error_mode, on_byte);
    }

    // 8. If code point is U+2212, set it to U+FF0D.
    if (item == 0x2212)
        item = 0xFF0D;

    // 9. If code point is in the range U+FF61 to U+FF9F, inclusive, set it to the index code point for code point − 0xFF61
    //    in index ISO-2022-JP katakana.
    if (item >= 0xFF61 && item <= 0xFF9F) {
        item = *index_iso_2022_jp_katakana_code_point(item - 0xFF61);
    }

    // 10. Let pointer be the index pointer for code point in index jis0208.
    auto pointer = code_point_jis0208_index(item);

    // 11. If pointer is null, then:
    if (!pointer.has_value()) {
        // 1. If ISO-2022-JP encoder state is jis0208, then restore code point to ioQueue, set ISO-2022-JP encoder state to
        //    ASCII, and return three bytes 0x1B 0x28 0x42.
        if (state == State::jis0208) {
            TRY(on_byte(0x1B, AlwaysEscape::No));
            TRY(on_byte(0x28, AlwaysEscape::No));
            TRY(on_byte(0x4A, AlwaysEscape::No));
            return process_item(item, State::ASCII, error_mode, on_byte);
        }

        // 2. Return error with code point.
        TRY(handle_error(error_mode, item, on_byte));
        return state;
    }

    // 12. If ISO-2022-JP encoder state is not jis0208, restore code point to ioQueue, set ISO-2022-JP encoder state to
    //     jis0208, and return three bytes 0x1B 0x24 0x42.
    if (state != State::jis0208) {
        TRY(on_byte(0x1B, AlwaysEscape::No));
        TRY(on_byte(0x24, AlwaysEscape::No));
        TRY(on_byte(0x42, AlwaysEscape::No));
        return process_item(item, State::jis0208, error_mode, on_byte);
    }

    // 13. Let lead be pointer / 94 + 0x21.
    auto lead = *pointer / 94 + 0x21;

    // 14. Let trail be pointer % 94 + 0x21.
    auto trail = *pointer % 94 + 0x21;

    // 15. Return two bytes whose values are lead and trail.
    TRY(on_byte(static_cast<u8>(lead), AlwaysEscape::No));
    TRY(on_byte(static_cast<u8>(trail), AlwaysEscape::No));
    return state;
}

// https://encoding.spec.whatwg.org/#iso-2022-jp-encoder
ErrorOr<void> ISO2022JPEncoder::process(Utf8View input, ErrorMode error_mode, Function<ErrorOr<void>(u8, AlwaysEscape)> on_byte)
{
    // ISO-2022-JP’s encoder has an associated ISO-2022-JP encoder state which is ASCII, Roman, or jis0208 (initially ASCII).
    auto state = State::ASCII;

    for (u32 item : input) {
        state = TRY(process_item(item, state, error_mode, on_byte));
    }

    // 1. If code point is end-of-queue and ISO-2022-JP encoder state is not ASCII, set ISO-2022-JP
    //    encoder state to ASCII, and return three bytes 0x1B 0x28 0x42.
    if (state != State::ASCII) {
        state = State::ASCII;
        TRY(on_byte(0x1B, AlwaysEscape::No));
        TRY(on_byte(0x28, AlwaysEscape::No));
        TRY(on_byte(0x42, AlwaysEscape::No));
        return {};
    }

    // 2. If code point is end-of-queue and ISO-2022-JP encoder state is ASCII, return finished.
    return {};
}

// https://encoding.spec.whatwg.org/#index-shift_jis-pointer
static Optional<u32> index_shift_jis_pointer(u32 code_point)
{
    // 1. Let index be index jis0208 excluding all entries whose pointer is in the range 8272 to 8835, inclusive.
    auto pointer = code_point_jis0208_index(code_point);
    if (!pointer.has_value())
        return {};
    if (*pointer >= 8272 && *pointer <= 8835)
        return {};

    // 2. Return the index pointer for code point in index.
    return *pointer;
}

// https://encoding.spec.whatwg.org/#shift_jis-encoder
ErrorOr<void> ShiftJISEncoder::process(Utf8View input, ErrorMode error_mode, Function<ErrorOr<void>(u8, AlwaysEscape)> on_byte)
{
    for (u32 item : input) {
        // 1. If code point is end-of-queue, return finished.

        // 2. If code point is an ASCII code point or U+0080, return a byte whose value is code point.
        if (item <= 0x0080) {
            TRY(on_byte(static_cast<u8>(item), AlwaysEscape::No));
            continue;
        }

        // 3. If code point is U+00A5, return byte 0x5C.
        if (item == 0x00A5) {
            TRY(on_byte(0x5C, AlwaysEscape::No));
            continue;
        }

        // 4. If code point is U+203E, return byte 0x7E.
        if (item == 0x203E) {
            TRY(on_byte(0x7E, AlwaysEscape::No));
            continue;
        }

        // 5. If code point is in the range U+FF61 to U+FF9F, inclusive, return a byte whose value is code point − 0xFF61 + 0xA1.
        if (item >= 0xFF61 && item <= 0xFF9F) {
            TRY(on_byte(static_cast<u8>(item - 0xFF61 + 0xA1), AlwaysEscape::No));
            continue;
        }

        // 6. If code point is U+2212, set it to U+FF0D.
        if (item == 0x2212)
            item = 0xFF0D;

        // 7. Let pointer be the index Shift_JIS pointer for code point.
        auto pointer = index_shift_jis_pointer(item);

        // 8. If pointer is null, return error with code point.
        if (!pointer.has_value()) {
            TRY(handle_error(error_mode, item, on_byte));
            continue;
        }

        // 9. Let lead be pointer / 188.
        auto lead = *pointer / 188;

        // 10. Let lead offset be 0x81 if lead is less than 0x1F, otherwise 0xC1.
        auto lead_offset = 0xC1;
        if (lead < 0x1F)
            lead_offset = 0x81;

        // 11. Let trail be pointer % 188.
        auto trail = *pointer % 188;

        // 12. Let offset be 0x40 if trail is less than 0x3F, otherwise 0x41.
        auto offset = 0x41;
        if (trail < 0x3F)
            offset = 0x40;

        // 13. Return two bytes whose values are lead + lead offset and trail + offset.
        TRY(on_byte(static_cast<u8>(lead + lead_offset), AlwaysEscape::No));
        TRY(on_byte(static_cast<u8>(trail + offset), AlwaysEscape::No));
    }

    return {};
}

// https://encoding.spec.whatwg.org/#euc-kr-encoder
ErrorOr<void> EUCKREncoder::process(Utf8View input, ErrorMode error_mode, Function<ErrorOr<void>(u8, AlwaysEscape)> on_byte)
{
    for (u32 item : input) {
        // 1. If code point is end-of-queue, return finished.

        // 2. If code point is an ASCII code point, return a byte whose value is code point.
        if (item < 0x0080) {
            TRY(on_byte(static_cast<u8>(item), AlwaysEscape::No));
            continue;
        }

        // 3. Let pointer be the index pointer for code point in index EUC-KR.
        auto pointer = code_point_euc_kr_index(item);

        // 4. If pointer is null, return error with code point.
        if (!pointer.has_value()) {
            TRY(handle_error(error_mode, item, on_byte));
            continue;
        }

        // 5. Let lead be pointer / 190 + 0x81.
        auto lead = *pointer / 190 + 0x81;

        // 6. Let trail be pointer % 190 + 0x41.
        auto trail = *pointer % 190 + 0x41;

        // 7. Return two bytes whose values are lead and trail.
        TRY(on_byte(static_cast<u8>(lead), AlwaysEscape::No));
        TRY(on_byte(static_cast<u8>(trail), AlwaysEscape::No));
    }

    return {};
}

// https://encoding.spec.whatwg.org/#big5-encoder
ErrorOr<void> Big5Encoder::process(Utf8View input, ErrorMode error_mode, Function<ErrorOr<void>(u8, AlwaysEscape)> on_byte)
{
    for (u32 item : input) {
        // 1. If code point is end-of-queue, return finished.

        // 2. If code point is an ASCII code point, return a byte whose value is code point.
        if (item < 0x0080) {
            TRY(on_byte(static_cast<u8>(item), AlwaysEscape::No));
            continue;
        }

        // 3. Let pointer be the index Big5 pointer for code point.
        auto pointer = code_point_big5_index(item);

        // 4. If pointer is null, return error with code point.
        if (!pointer.has_value()) {
            TRY(handle_error(error_mode, item, on_byte));
            continue;
        }

        // 5. Let lead be pointer / 157 + 0x81.
        auto lead = *pointer / 157 + 0x81;

        // 6. Let trail be pointer % 157.
        auto trail = *pointer % 157;

        // 7. Let offset be 0x40 if trail is less than 0x3F, otherwise 0x62.
        auto offset = 0x62;
        if (trail < 0x3f)
            offset = 0x40;

        // 8. Return two bytes whose values are lead and trail + offset.
        TRY(on_byte(static_cast<u8>(lead), AlwaysEscape::No));
        TRY(on_byte(static_cast<u8>(trail + offset), AlwaysEscape::No));
    }

    return {};
}

// https://encoding.spec.whatwg.org/#index-gb18030-ranges-pointer
static u32 index_gb18030_ranges_pointer(u32 code_point)
{
    // 1. If code point is U+E7C7, return pointer 7457.
    if (code_point == 0xe7c7)
        return 7457;

    // 2. Let offset be the last code point in index gb18030 ranges that is less than
    //    or equal to code point and let pointer offset be its corresponding pointer.
    size_t last_index;
    binary_search(s_gb18030_ranges, code_point, &last_index, [](auto const code_point, auto const& entry) {
        return code_point - entry.code_point;
    });
    auto offset = s_gb18030_ranges[last_index].code_point;
    auto pointer_offset = s_gb18030_ranges[last_index].pointer;

    // 3. Return a pointer whose value is pointer offset + code point − offset.
    return pointer_offset + code_point - offset;
}

GB18030Encoder::GB18030Encoder(IsGBK is_gbk)
    : m_is_gbk(is_gbk)
{
}

// https://encoding.spec.whatwg.org/#gb18030-encoder
ErrorOr<void> GB18030Encoder::process(Utf8View input, ErrorMode error_mode, Function<ErrorOr<void>(u8, AlwaysEscape)> on_byte)
{
    bool gbk = (m_is_gbk == IsGBK::Yes);

    for (u32 item : input) {
        // 1. If code point is end-of-queue, return finished.

        // 2. If code point is an ASCII code point, return a byte whose value is code point.
        if (item < 0x0080) {
            TRY(on_byte(static_cast<u8>(item), AlwaysEscape::No));
            continue;
        }

        // 3. If code point is U+E5E5, return error with code point.
        if (item == 0xE5E5) {
            TRY(handle_error(error_mode, item, on_byte));
            continue;
        }

        // 4. If is GBK is true and code point is U+20AC, return byte 0x80.
        if (gbk && item == 0x20AC) {
            TRY(on_byte(0x80, AlwaysEscape::No));
            continue;
        }

        // 5. Let pointer be the index pointer for code point in index gb18030.
        auto pointer = code_point_gb18030_index(item);

        // 6. If pointer is non-null, then:
        if (pointer.has_value()) {
            // 1. Let lead be pointer / 190 + 0x81.
            auto lead = *pointer / 190 + 0x81;

            // 2. Let trail be pointer % 190.
            auto trail = *pointer % 190;

            // 3. Let offset be 0x40 if trail is less than 0x3F, otherwise 0x41.
            auto offset = 0x41;
            if (trail < 0x3f)
                offset = 0x40;

            // 4. Return two bytes whose values are lead and trail + offset.
            TRY(on_byte(static_cast<u8>(lead), AlwaysEscape::No));
            TRY(on_byte(static_cast<u8>(trail + offset), AlwaysEscape::No));
            continue;
        }

        // 7. If is GBK is true, return error with code point.
        if (gbk) {
            TRY(handle_error(error_mode, item, on_byte));
            continue;
        }

        // 8. Set pointer to the index gb18030 ranges pointer for code point.
        pointer = index_gb18030_ranges_pointer(item);

        // 9. Let byte1 be pointer / (10 × 126 × 10).
        auto byte1 = *pointer / (10 * 126 * 10);

        // 10. Set pointer to pointer % (10 × 126 × 10).
        pointer = *pointer % (10 * 126 * 10);

        // 11. Let byte2 be pointer / (10 × 126).
        auto byte2 = *pointer / (10 * 126);

        // 12. Set pointer to pointer % (10 × 126).
        pointer = *pointer % (10 * 126);

        // 13. Let byte3 be pointer / 10.
        auto byte3 = *pointer / 10;

        // 14. Let byte4 be pointer % 10.
        auto byte4 = *pointer % 10;

        // 15. Return four bytes whose values are byte1 + 0x81, byte2 + 0x30, byte3 + 0x81, byte4 + 0x30.
        TRY(on_byte(static_cast<u8>(byte1 + 0x81), AlwaysEscape::No));
        TRY(on_byte(static_cast<u8>(byte2 + 0x30), AlwaysEscape::No));
        TRY(on_byte(static_cast<u8>(byte3 + 0x81), AlwaysEscape::No));
        TRY(on_byte(static_cast<u8>(byte4 + 0x30), AlwaysEscape::No));
    }

    return {};
}

}
