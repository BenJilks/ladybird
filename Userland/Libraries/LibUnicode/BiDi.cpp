/*
 * Copyright (c) 2024, Ben Jilks <benjyjilks@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BiDi.h"
#include <AK/ScopeGuard.h>
#include <AK/String.h>
#include <AK/Utf16View.h>
#include <AK/Vector.h>

#include <unicode/ubidi.h>
#include <unicode/uchar.h>

namespace Unicode {

static UBiDiLevel direction_to_bidi_level(BiDiDirection direction)
{
    switch (direction) {
    case BiDiDirection::Ltr:
        return 0;
    case BiDiDirection::Rtl:
        return 1;
    default:
        VERIFY_NOT_REACHED();
    }
}

ErrorOr<String> render_directional_paragraph(StringView paragraph, BiDiDirection direction)
{
    if (paragraph.is_empty())
        return String {};

    auto* bidi = ubidi_open();
    ScopeGuard const close_bidi = [&]() { ubidi_close(bidi); };
    auto chars = TRY(utf8_to_utf16(paragraph));

    UErrorCode error_code = U_ZERO_ERROR;
    ubidi_setPara(bidi, reinterpret_cast<UChar*>(chars.data()), static_cast<int32_t>(chars.size()), direction_to_bidi_level(direction), nullptr, &error_code);
    if (U_FAILURE(error_code))
        return Error::from_string_view({ u_errorName(error_code), strlen(u_errorName(error_code)) });

    Vector<u16> output;
    output.resize(ubidi_getResultLength(bidi));

    error_code = U_ZERO_ERROR;
    ubidi_writeReordered(bidi, reinterpret_cast<UChar*>(output.data()), static_cast<int32_t>(output.size()), 0, &error_code);
    if (U_FAILURE(error_code))
        return Error::from_string_view({ u_errorName(error_code), strlen(u_errorName(error_code)) });

    Utf16View output_string(output.span());
    return output_string.to_utf8();
}

}
