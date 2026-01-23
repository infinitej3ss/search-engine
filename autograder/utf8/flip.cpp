#include "Utf8.h"
#include <iostream>
#include <fstream>

// check if c is replacement char before returning
void Utf8ToUtf16(const Utf8 **p, const Utf8 *bound) {
  Utf16 buffer[2];

  std::cout << ByteOrderMark;

  for(;;) {
    Unicode c = ReadUtf8(p, bound);

    if (*p >= bound && !c) return;

    size_t size = SizeOfUtf16(c);
    Utf16* end = WriteUtf16(buffer, c);

    std::cout << buffer[0];

    if (size > 2) {
      std::cout << buffer[1];
    }
  }
}

void Utf16ToUtf8(const Utf16 **p, const Utf16 *bound, bool big_endian) {
  Utf8 buffer[6];

  for(;;) {
    Unicode c = ReadUtf16(p, bound);

    if (*p >= bound) return;

    size_t size = SizeOfUtf8(c);
    Utf8* end = WriteUtf8(buffer, c);

    for (size_t i = 0; i < size; i++) {
      std::cout << buffer[i];
    }
  }
}

int main(int argc, char *argv[]) {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  if (argc < 2) {
    std::cout << "Usage: flip <filename>\n";
    std::cout << "Recognize the input file as either Utf8 or Utf16, convert\n";
    std::cout << "it to the opposite encoding, and write the result to stdout.\n";

    return 1;
  }

  // open fstream
  std::ifstream Input;
  Input.open(argv[1], std::ios::binary|std::ios::ate);

  if (Input.fail()) {
    std::cout << argv[1] << " not found.\n";
    return 1;
  }

  // recommend reading the whole file into memory as binary data to start
  // Source - https://stackoverflow.com/a
  // Posted by David Haim, modified by community. See post 'Timeline' for change history

  // Retrieved 2026-01-23, License - CC BY-SA 3.0

  auto fileSize = Input.tellg();
  Input.seekg(std::ios::beg);
  std::string content(fileSize,0);
  Input.read(&content[0],fileSize);

  // check for type
  bool big_endian = false;
  bool is_utf8 = false;

  if (content.size() >= 2) { // enough space for a bom?
    char first = content[0];
    char second = content[1];

    // turn it into unicode
    Unicode read_bom = ((first << 8) | second);
    if (read_bom == ByteOrderMark) {
      // Utf16
      // do nothing
    }
    else if (read_bom == BigEndianBOM) {
      // bigend Utf16
      big_endian = true;
    }
    else {
      is_utf8 = true;
    }
  }
  else {
    is_utf8 = true;
  }

  // time to actually do stuff
  if (is_utf8) {
    const Utf8* p = reinterpret_cast<const Utf8*>(content.data());
    const Utf8* end = p + content.size();

    Utf8ToUtf16(&p, end);
  }
  else {
    const Utf16* p = reinterpret_cast<const Utf16*>(content.data());
    const Utf16* end = p + content.size() / sizeof(Utf16);

    Utf16ToUtf8(&p, end, big_endian);
  }
}


