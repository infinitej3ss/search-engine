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

// tried making a lookup table, performance was basically the same
size_t IndicatedLength(const Utf8* p) {
    Utf8 b = *p;

    if (b < 0x80) return 1;
    if ((b & 0b11100000) == 0b11000000) return 2;
    if ((b & 0b11110000) == 0b11100000) return 3;
    if ((b & 0b11111000) == 0b11110000) return 4;
    if ((b & 0b11111100) == 0b11111000) return 5;
    if ((b & 0b11111110) == 0b11111100) return 6;

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

static constexpr Unicode kMinUtf8Values[7] = {
    0, 0x0, 0x80, 0x800, 0x10000, 0x200000, 0x4000000
};


Unicode ReadUtf8(const Utf8** p, const Utf8* bound) {
    if (p == nullptr || bound != nullptr && *p >= bound) return 0;

    const Utf8* cur = *p;
    Utf8 lead = *cur;

    // fast ascii check
    if (lead < 0x80) {
        (*p)++;
        return lead;
    }

    // unexpected continuation byte
    if ((lead & 0xc0) == 0x80) {
        *p = cur + 1;
        return ReplacementCharacter;
    }

    // guess at length
    // replacing IndicatedLength with a quick check to avoid pointer dereference + fx call
    size_t len;
    Utf8 b = lead;

    // ascii check above
    if (b < 0xe0) len = 2;
    else if (b < 0xf0) len = 3;
    else if (b < 0xf8) len = 4;
    else if (b < 0xfc) len = 5;
    else if (b < 0xfe) len = 6;
    else len = 1;

    // cond. 2: runs past last valid byte
    if (bound != nullptr && *p + len > bound) {
        *p = bound;
        return ReplacementCharacter;
    }

    Unicode c = 0;

    // unroll common cases
    switch (len) {
        case 2: {
            Utf8 b1 = cur[1];
            if ((b1 & 0xc0) != 0x80) {
                *p = cur + 1;
                return ReplacementCharacter;
            }
            c = ((lead & 0x1f) << 6) | (b1 & 0x3f);
            if (c < 0x80) {
                *p = cur + 2;
                return ReplacementCharacter;
            }
            *p = cur + 2;
            return c;
        }
        case 3: {
            Utf8 b1 = cur[1], b2 = cur[2];
            if (((b1 & 0xc0) != 0x80) || ((b2 & 0xc0) != 0x80)) {
                *p = cur + ((b1 & 0xc0) != 0x80 ? 1 : 2);
                return ReplacementCharacter;
            }
            c = ((lead & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f);
            if (c < 0x800 || (c >= 0xd800 && c <= 0xdfff) || 
                (c >= 0xfdd0 && c <= 0xfdef) || c == 0xfffe || c == 0xffff) {
                *p = cur + 3;
                return ReplacementCharacter;
            }
            *p = cur + 3;
            return c;
        }
        case 4: {
            Utf8 b1 = cur[1], b2 = cur[2], b3 = cur[3];
            if (((b1 & 0xc0) != 0x80) || ((b2 & 0xc0) != 0x80) || ((b3 & 0xc0) != 0x80)) {
                *p = cur + ((b1 & 0xc0) != 0x80 ? 1 : (b2 & 0xc0) != 0x80 ? 2 : 3);
                return ReplacementCharacter;
            }
            c = ((lead & 0x07) << 18) | ((b1 & 0x3f) << 12) | 
                ((b2 & 0x3f) << 6) | (b3 & 0x3f);
            if (c < 0x10000) {
                *p = cur + 4;
                return ReplacementCharacter;
            }
            *p = cur + 4;
            return c;
        }
        default: {
            // len 5, 6, or invalid
            if (len == 1) {
                (*p)++;
                return ReplacementCharacter;
            }

            c = lead & (0x7f >> len);
            for (size_t i = 1; i < len; i++) {
                if ((cur[i] & 0xc0) != 0x80) {
                    *p = cur + i;
                    return ReplacementCharacter;
                }
                c = (c << 6) | (cur[i] & 0x3f);
            }
            
            //overlong
            if (c < kMinUtf8Values[len]) {
                *p = cur + len;
                return ReplacementCharacter;
            }

            *p = cur + len;
            return c;
        }
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
    // fast ascii
    if (c < 0x80) {
        if (bound != nullptr && p >= bound) return p;
        *p = static_cast<Utf8>(c);
        return p + 1;
    }

    // fast 2 bit
    if (c < 0x800) {
        if (bound != nullptr && p + 2 > bound) return p;
        p[1] = 0x80 | (c & 0x3f);
        p[0] = 0xc0 | (c >> 6);
        return p + 2;
    }

    if (c > 0x7fffffff) c = ReplacementCharacter;
    size_t len = SizeOfUtf8(c);

    if (bound != nullptr && p + len > bound) return p;

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
    if (bound != nullptr && *p >= bound) return 0;

    Utf16 first = **p;
    (*p)++;

    // fast path: not surrogate
    if (first < 0xd800 || first > 0xdfff) {
        return first;
    }

    // check for high surrogate
    if (first <= 0xdbff) {
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

        // out-of-order or unpaired high surrogate, return as-is
        return first;
    }

    // regular character or low surrogate (unpaired), return as-is
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
    // fast path: BMP, non-surrogate
    if (c <= 0xffff && (c < 0xd800 || c > 0xdfff)) {
        if (bound != nullptr && p >= bound) return p;
        *p++ = static_cast<Utf16>(c);
        return p;
    }

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
    if (bound == nullptr) {
        if (*p < 0x80) return p + 1;
    } else {
        if (p < bound && *p < 0x80) return p + 1;
    }

    ReadUtf8(&p, bound);
    return p;
}

const Utf16* NextUtf16(const Utf16* p, const Utf16* bound) {
    ReadUtf16(&p, bound);
    return p;
}

