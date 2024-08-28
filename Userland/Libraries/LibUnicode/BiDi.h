/*
 * Copyright (c) 2024, Ben Jilks <benjyjilks@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>

namespace Unicode {

enum class BiDiDirection {
    Ltr,
    Rtl,
};

ErrorOr<String> render_directional_paragraph(StringView, BiDiDirection);

}
