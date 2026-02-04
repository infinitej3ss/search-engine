#include <cassert>
#include <cstring>
#include <iostream>
#include "HtmlTags.h"

/*
   OrdinaryText,
   Title,
   Comment,
   Discard,
   DiscardSection,
   Anchor,
   Base,
   Embed
*/

// Define a macro for quick testing
#define TEST(tag) LookupPossibleTag(tag, tag + strlen(tag))

void TestLookupPossibleTag() {
    // Different tests for different edge cases
    // Exact matches
    assert(TEST("title") == DesiredAction::Title);
    assert(TEST("!--") == DesiredAction::Comment);
    assert(TEST("a") == DesiredAction::Anchor);

    // Case sensitive matches
    assert(TEST("TiTlE") == DesiredAction::Title);
    assert(TEST("A") == DesiredAction::Anchor);

    // Prefix but not equal
    assert(TEST("tit") == DesiredAction::OrdinaryText);

    // Superset but not equal
    assert(TEST("titlex") == DesiredAction::OrdinaryText);

    // Unknown Tag
    assert(TEST("baddies") == DesiredAction::OrdinaryText);

    // Empty Tag
    assert(TEST("") == DesiredAction::OrdinaryText);

    // Discard Tag
    assert(TEST("!doctype") == DesiredAction::Discard);

    // Embed Tag
    assert(TEST("embed") == DesiredAction::Embed);

    // Base Tag
    assert(TEST("base") == DesiredAction::Base);

    // DiscardSection Tags
    assert(TEST("script") == DesiredAction::DiscardSection);
    assert(TEST("svg") == DesiredAction::DiscardSection);
    assert(TEST("style") == DesiredAction::DiscardSection);

    std::cout << "All tests passed!\n";
}

int main() {
    TestLookupPossibleTag();
    return 0;
}