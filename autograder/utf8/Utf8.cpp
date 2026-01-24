#include "Utf8.h"

// SizeOfUtf8 tells the number of bytes it will take to encode the
// specified value as Utf8.  Assumes values over 31 bits will be replaced.

// SizeOfUTF8( GetUtf8( p ) ) does not tell how many bytes encode
// the character pointed to by p because p may point to a malformed
// character.

size_t SizeOfUtf8(Unicode c) {
    //   U-00000000 - U-0000007F:   0xxxxxxx 7 bits
    //   U-00000080 - U-000007FF:   110xxxxx 10xxxxxx 11 bits
    //   U-00000800 - U-0000FFFF:   1110xxxx 10xxxxxx 10xxxxxx 16 bits
    //   U-00010000 - U-001FFFFF:   11110xxx 10xxxxxx 10xxxxxx 10xxxxxx 21 bits
    //   U-00200000 - U-03FFFFFF:   111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 26 bits
    //   U-04000000 - U-7FFFFFFF:   1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 31 bits

    if (c <= 0x7F) return 1;
    if (c <= 0x7FF) return 2;
    if (c <= 0xFFFF) return 3;
    if (c <= 0x1FFFFF) return 4;
    if (c <= 0x3FFFFFF) return 5;
    if (c <= 0x7FFFFFFF) return 6;
    return 3;  // replacement char
}

// SizeOfUtf16 tells the number of bytes it will take to encode the
// specified value as Utf16. Assumes values over 0xffff will be written as
// surrogates and that values > 0x10ffff will be replaced.

size_t SizeOfUtf16(Unicode c) {
    // check for replacement
    if (c > 0x10ffff || c <= 0xffff) {
        return 2;  // for replacement char or fits
    } else {
        return 4;  // two 16-bit surrogates
    }
}

// IndicatedLength looks at the first byte of a Utf8 sequence and
// determines the expected length.  Return 1 for an invalid first byte.

size_t IndicatedLength(const Utf8* p) {
    Utf8 b = *p;

    // check for 1 byte
    if ((b & 0x80) == 0) {
        return 1;
    }

    for (size_t len = 2; len <= 6; len++) {
        // shift b right to leave only the leftmost len bits
        Utf8 top_bits = b >> (8 - len - 1);

        // create expected pattern: len ones followed by a 0
        Utf8 pattern = ((1 << len) - 1) << 1;

        if (top_bits == pattern) {
            return len;
        }
    }

    return 1;  // invalid first byte
}

// Read the next Utf8 character beginning at **pp and bump *pp past
// the end of the character.  if bound != null, bound points one past
// the last valid byte.
//
// 1.  If *pp already points to or past the last valid byte, do not
//     advance *pp and return the null character.
// 2.  If there is at least one byte, but the character runs past the
//     last valid byte or is invalid, return the replacement character
//     and set *pp pointing to just past the last byte consumed.
//
// Overlong characters and character values 0xd800 to 0xdfff (UTF-16
// surrogates), "noncharacters" 0xfdd0 to 0xfdef, and the values 0xfffe
// and 0xffff should be returned as the replacement character.

Unicode ReadUtf8(const Utf8** p, const Utf8* bound) {
    if (p == nullptr) return 0;

    // cond. 1: *pp points to or past last valid byte
    if (bound != nullptr && *p >= bound) {
        return 0;
    }

    // guess at length
    size_t len = IndicatedLength(*p);

    // cond. 2: runs past last valid byte
    if (bound != nullptr && *p + len > bound) {
        *p = bound;
        return ReplacementCharacter;
    }

    Unicode c = 0;

    if (len == 1) {
        // check for malformed byte
        if ((*p)[0] & 0x80) {
            (*p)++;
            return ReplacementCharacter;
        }
        c = (*p)[0] & 0x7f;
    } else {
        // get bits from first byte (varies by length)
        c = (*p)[0] & (0x7f >> len);

        // get 6 bits from each continuation byte
        for (size_t i = 1; i < len; i++) {
            // check if is valid continuation byte (starts with 0b10)
            if (((*p)[i] >> 6) != 0b10) {
                *p += i;
                return ReplacementCharacter;
            }
            c = (c << 6) | ((*p)[i] & 0b00111111);
        }
    }

    // overlong
    if (SizeOfUtf8(c) != len) {
        *p += len;
        return ReplacementCharacter;
    }

    // Overlong characters and character values 0xd800 to 0xdfff (UTF-16
    // surrogates), "noncharacters" 0xfdd0 to 0xfdef, and the values 0xfffe
    // and 0xffff should be returned as the replacement character.

    if ((c >= 0xd800 && c <= 0xdfff) ||
        (c >= 0xfdd0 && c <= 0xfdef) ||
        c == 0xfffe || c == 0xffff) {
        *p += len;
        return ReplacementCharacter;
    }

    *p += len;
    return c;
}

// Scan backward for the first PREVIOUS byte which could
// be the start of a UTF-8 character.  If bound != null,
// bound points to the first (leftmost) valid byte.

const Utf8* PreviousUtf8(const Utf8* p, const Utf8* bound) {
    for (;;) {
        if (bound != nullptr && p <= bound) return p;  // hit boundary
        p--;
        if ((*p & 0b11000000) != 0b10000000) return p;  // if is not continuation
    }
}

// Write a Unicode character in UTF-8, returning one past
// the the last byte that was written.
//
// If bound != null, it points one past the last valid location
// in the buffer. (If p is already at or past the bound, do
// nothing and return p.)
//
// If c > 0x7fffffff (31 bits) write the replacement character.

Utf8* WriteUtf8(Utf8* p, Unicode c, Utf8* bound) {
    size_t len = SizeOfUtf8(c);
    if (bound != nullptr && p + len > bound) return p;

    if (c > 0x7fffffff) {
        c = ReplacementCharacter;
    }

    // write continuation bytes (if any), from back to front
    for (size_t i = len; i-- > 1;) {
        p[i] = static_cast<Utf8>(0b10000000 | (c & 0b00111111));
        c >>= 6;
    }

    // leading byte:
    // for len = 1: mask = 0x00, payload = 7 bits
    // for len > 1: mask = ((1 << len) - 1) << (8 - len)
    if (len == 1) {
        p[0] = static_cast<Utf8>(c & 0x7F);
    } else {
        Utf8 lead_mask = static_cast<Utf8>(((1u << len) - 1) << (8 - len));
        p[0] = static_cast<Utf8>(lead_mask | c);
    }

    return p + len;
}

// Read a Utf16 little-endian character beginning at **pp and
// bump *pp past the end of the character.  if bound != null,
// bound points one past the last valid byte.  If *pp >= bound,
// do not advance *pp and return the null character.
//
// Values over 16-bits are read as a high surrogate followed by
// a low surrogate. Unpaired or out-of-order surrogates are
// read as literal values.

Unicode ReadUtf16(const Utf16** p, const Utf16* bound) {
    Utf16 first = **p;

    if (bound != nullptr && *p >= bound) return 0;

    (*p)++;

    // check for high surrogate
    if (first >= 0xd800 && first <= 0xdbff) {
        if (bound != nullptr && *p >= bound) {
            return first;  // unpaired due to bound, return literal
        }

        Utf16 second = **p;

        // check for low surrogate
        if (second >= 0xdc00 && second <= 0xdfff) {
            (*p)++;

            Unicode c = 0x10000 + ((first & 0b1111111111) << 10) + (second & 0b1111111111);
            return c;
        }

        // out-of-order or unpaired
        return first;
    }

    // not a surrogate, return as-is
    return first;
}

// Utf16 uses aligned 16-bit characters in either big-endian (high byte first)
// or little-endian (low byte first) format to represent Unicode values in
// the range from 0x00 to 0x10ffff (21 bits).

// Values 0x0000 to 0xffff are written as a single 16-bit character.
//
// Values > 0x10ffff (21 bits) are written as replacement characters.
//
// Values 0x10000 to 0x10ffff are written as two 16-bit surrogates.
//
// 1.  0x10000 is subtracted from the Unicode value.
// 2.  The High Surrogate is written first as 0xd800 + high 10 bits.
// 5.  The Low Surrogate is written second as 0xdc00 + low 10 bits.
// 6.  Unpaired / out-of-ordersurrogates are read as their literal 16-bit values.
//
// Reading consists of reversing the steps.

// Write a Unicode character in UTF-16, returning one past
// the the last byte that was written.
//
// If bound != null, it points one past the last valid location
// in the buffer. (If p is already at or past the bound, do
// nothing and return p.)
//
// If c > 0x10ffff (21 bits) write the replacement character.

Utf16* WriteUtf16(Utf16* p, Unicode c, Utf16* bound) {
    size_t needed = SizeOfUtf16(c) / 2;

    // bound check
    if (bound != nullptr && p + needed > bound) return p;

    if (c <= 0xffff) {
        // literal
        *p++ = c;
    } else if (c > 0x10ffff) {
        // replacement
        *p++ = ReplacementCharacter;
    } else {
        // needs surrogate

        // 1.  0x10000 is subtracted from the Unicode value.
        // 2.  The High Surrogate is written first as 0xd800 + high 10 bits.
        // 5.  The Low Surrogate is written second as 0xdc00 + low 10 bits.
        // 6.  Unpaired / out-of-ordersurrogates are read as their literal 16-bit values.

        c -= 0x10000;
        Utf16 high = 0xd800 + ((c >> 10) & 0b1111111111);
        Utf16 low = 0xdc00 + (c & 0b1111111111);

        *p++ = high;
        *p++ = low;
    }

    return p;
}

// Wrappers that read the character but don't advance the pointer.

Unicode GetUtf8(const Utf8* p, const Utf8* bound) {
    // copy the pointer
    const Utf8* temp = p;
    return ReadUtf8(&temp, bound);
}

Unicode GetUtf16(const Utf16* p, const Utf16* bound) {
    const Utf16* temp = p;
    return ReadUtf16(&temp, bound);
}

// Wrappers that advance the pointer but throw away the value.

const Utf8* NextUtf8(const Utf8* p, const Utf8* bound) {
    ReadUtf8(&p, bound);
    return p;
}

const Utf16* NextUtf16(const Utf16* p, const Utf16* bound) {
    ReadUtf16(&p, bound);
    return p;
}
