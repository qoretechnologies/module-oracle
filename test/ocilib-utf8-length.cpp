// Copyright (C) 2026 Qore Technologies, s.r.o.
// SPDX-License-Identifier: MIT
// Link with the built oracle module and libqore; no database is needed.
#include <cstdio>
#include <string>

extern int OCI_StringUTF8Length(const char* str);

int main() {
    struct Case {
        const char* input;
        int length;
    };
    const Case cases[] = {
        {"", 0}, {"a", 1}, {"abc", 3},
        {"\xc3\xa9", 1}, {"\xe2\x82\xac", 1}, {"\xf0\x9f\x98\x80", 1},
        {"a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80", 4},
        {"first\0ignored", 5},
        // This helper counts leading bytes; it does not validate UTF-8.
        {"\x80\xbf", 0}, {"\xe2\x82", 1},
    };
    for (const Case& entry : cases) {
        if (OCI_StringUTF8Length(entry.input) != entry.length) {
            std::fprintf(stderr, "incorrect UTF-8 length (expected %d)\n", entry.length);
            return 1;
        }
    }
    const std::string large(1024 * 1024, 'a');
    if (OCI_StringUTF8Length(large.c_str()) != static_cast<int>(large.size())) {
        std::fprintf(stderr, "incorrect large-input length\n");
        return 1;
    }
    std::puts("UTF-8 length: empty, ASCII, multibyte, mixed, malformed, and large inputs passed");
    return 0;
}
