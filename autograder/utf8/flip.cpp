#include <fstream>
#include <iostream>

#include "Utf8.h"

void Utf16BigEndianToLittleEndian(Utf16* p, const size_t length) {
    for (size_t i = 0; i < length; i++) {
        *p = ((*p & 0xFF) << 8) | ((*p & 0xFF00) >> 8);

        p++;
    }
}

void Utf8ToUtf16(const Utf8** p, const Utf8* bound) {
    Utf16 buffer[2];

    fwrite((uint16_t*)&ByteOrderMark, sizeof(uint16_t), 1, stdout);
    bool first = true;

    for (;;) {
        const Utf8* p_prev = *p;
        Unicode c = ReadUtf8(p, bound);

        if ((*p >= bound && !c) || (*p == p_prev)) return;

        if (first) {
            first = false;
            if (c == ByteOrderMark || c == BigEndianBOM) {
                continue;
            }
        }

        size_t size = SizeOfUtf16(c);
        WriteUtf16(buffer, c);

        fwrite(buffer, sizeof(Utf16), 1 + (size > 2), stdout);
    }
}

void Utf16ToUtf8(const Utf16** p, const Utf16* bound) {
    Utf8 buffer[6];

    (*p)++;  // skip BOM

    for (;;) {
        const Utf16* p_prev = *p;
        Unicode c = ReadUtf16(p, bound);

        if (*p == p_prev) return;

        size_t size = SizeOfUtf8(c);
        WriteUtf8(buffer, c);

        fwrite(buffer, sizeof(Utf8), size, stdout);
    }
}

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    if (argc < 2) {
        std::cerr << "Usage: flip <filename>\n";
        std::cerr << "Recognize the input file as either Utf8 or Utf16, convert\n";
        std::cerr << "it to the opposite encoding, and write the result to stdout.\n";

        return 1;
    }

    // open fstream
    std::ifstream Input;
    Input.open(argv[1], std::ios::binary | std::ios::ate);

    if (Input.fail()) {
        std::cerr << argv[1] << " not found.\n";
        return 1;
    }

    // recommend reading the whole file into memory as binary data to start
    // Source - https://stackoverflow.com/a
    // Posted by David Haim, modified by community. See post 'Timeline' for change history

    // Retrieved 2026-01-23, License - CC BY-SA 3.0

    auto fileSize = Input.tellg();
    Input.seekg(std::ios::beg);
    std::string content(fileSize, 0);
    Input.read(&content[0], fileSize);

    // check for type
    bool big_endian = false;
    bool is_utf8 = false;

    if (content.size() >= 2) {  // enough space for a bom?
        char first = content[0];
        char second = content[1];

        // turn it into unicode
        Unicode read_bom = (((Unicode)second & 0xFF) << 8) | ((Unicode)first & 0xFF);
        if (read_bom == ByteOrderMark) {
            // Utf16
            // do nothing
        } else if (read_bom == BigEndianBOM) {
            // bigend Utf16
            big_endian = true;
        } else {
            is_utf8 = true;
        }
    } else {
        is_utf8 = true;
    }

    // time to actually do stuff
    if (is_utf8) {
        const Utf8* p = reinterpret_cast<const Utf8*>(content.data());
        const Utf8* end = p + content.size();

        Utf8ToUtf16(&p, end);
    } else {
        const Utf16* p = reinterpret_cast<const Utf16*>(content.data());
        const Utf16* end = p + content.size() / sizeof(Utf16);

        if (big_endian) Utf16BigEndianToLittleEndian(reinterpret_cast<Utf16*>(&content[0]), content.size() / sizeof(Utf16));
        Utf16ToUtf8(&p, end);
    }
}
