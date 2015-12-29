// Copyright 2015 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// Some free functions for manipulating Strings as URIs. Wherever possible,
// these functions take const references to StringView to avoid unnecessary
// copies.

#include "android/base/Uri.h"

#include "android/base/StringFormat.h"

#include <stdlib.h>

namespace android {
namespace base {

// Function to encode an individual character and append it to *|res|
static void appendEncodedChar(char c, String* res)
{
    switch (c) {
        case '!':
        case '#':
        case '$':
        case '&':
        case '\'':
        case '(':
        case ')':
        case '*':
        case '+':
        case ',':
        case '/':
        case ':':
        case ';':
        case '=':
        case '?':
        case '@':
        case '[':
        case ']':
        case ' ':
        case '%':
            StringAppendFormat(res, "%%%02X", c);
            break;
        default:
            res->append(c);
            break;
    }
}

// static
String Uri::Encode(StringView uri) {
    String encodedUri;
    encodedUri.reserve(uri.size());
    for (const char c : uri) {
        appendEncodedChar(c, &encodedUri);
    }
    return encodedUri;
}

// static
String Uri::Decode(StringView uri) {
    String decodedUri;
    decodedUri.reserve(uri.size());
    for (StringView::const_iterator cit = uri.begin(); cit != uri.end();
         ++cit) {
        if (*cit == '%') {
            char hex[3];
            char decodedChar;

            ++cit;
            if (cit == uri.end()) {
                return "";
            }
            hex[0] = *cit;
            ++cit;
            if (cit == uri.end()) {
                return "";
            }
            hex[1] = *cit;

            hex[2] = '\0';
            decodedChar = static_cast<char>(strtoul(hex, NULL, 16));
            if (decodedChar == 0) {
                return "";
            }
            decodedUri.append(decodedChar);
        } else {
            decodedUri.append(*cit);
        }
    }
    return decodedUri;
}

String Uri::FormatHelper::encodeArg(StringView str)
{
    return Uri::Encode(str);
}

}  // namespace base
}  // namespace android
