// HtmlParser.h
// Nicole Hamilton, nham@umich.edu

#pragma once

#include <string>
#include <vector>
#include <string_view>

#include "HtmlTags.h"

// This is a simple HTML parser class.  Given a text buffer containing
// a presumed HTML page, the constructor will parse the text to create
// lists of words, title words and outgoing links found on the page.  It
// does not attempt to parse the entire the document structure.
//
// The strategy is to word-break at whitespace and HTML tags and discard
// most HTML tags.  Three tags require discarding everything between
// the opening and closing tag. Five tags require special processing.
//
// We will use the list of possible HTML element names found at
// https://developer.mozilla.org/en-US/docs/Web/HTML/Element +
// !-- (comment), !DOCTYPE and svg, stored as a table in HtmlTags.h.

// Here are the rules for recognizing HTML tags.
//
// 1. An HTML tag starts with either < if it's an opening tag or </ if it's
//    a closing token.  If it starts with < and ends with /> it is both.
//
// 2. The name of the tag must follow the < or </ immediately.  There can't
//    be any whitespace.
//
// 3. The name is terminated by whitespace, > or / and is case-insensitive.
//    The exception is <!--, which starts a comment and is not required
//    to be terminated.
//
// 4. If the name is terminated by whitepace, arbitrary text representing
//    various arguments may follow, terminated by a > or />.
//
// 5. If the name isn't on the list we recognize, we assume it's just
//    ordinary text.
//
// 6. Every token is taken as a word-break.
//
// 7. Most opening or closing tokens can simply be discarded.
//
// 8. <script>, <style>, and <svg> require discarding everything between the
//    opening and closing tag.  Unmatched closing tags are discarded.
//
// 9. <!--, <title>, <a>, <base> and <embed> require special processing.
//
//      <!-- is the beginng of a comment.  Everything up to the ending -->
//          is discarded.
//
//      <title> should cause all the words between the opening and closing
//          tags to be added to the titleWords vector rather than the default
//          words vector.  A closing </title> without an opening <title> is discarded.
//
//      <a> is expected to contain an href="...url..."> argument with the
//          URL inside the double quotes that should be added to the list
//          of links.  All the words in between the opening and closing tags
//          should be collected as anchor text associated with the link
//          in addition to being added to the words or titleWords vector,
//          as appropriate.  A closing </a> without an opening <a> is
//          discarded.
//
//     <base> may contain an href="...url..." parameter.  If present, it should
//          be captured as the base variable.  Only the first is recognized; any
//          others are discarded.
//
//     <embed> may contain a src="...url..." parameter.  If present, it should be
//          added to the links with no anchor text.

class Link {
   public:
    std::string URL;
    std::vector<std::string> anchorText;

    Link(std::string URL) : URL(URL) {
    }
};

class HtmlParser {
   public:
    std::vector<std::string> words, titleWords;
    std::vector<Link> links;
    std::string base;

   private:
    // YOUR CODE HERE

    std::vector<std::string>* push_words_to = &words;
    bool in_anchor = false;
    Link anchor = Link("");

    // store word between sofWord and eofWord in words or titleWords depending on in_title
    void store_word(const char* sofWord, const char* eofWord) {
        std::string s = std::string(sofWord, eofWord);
        if(s.size() > __UINT16_MAX__) {
            return;
        }
        (*push_words_to).push_back(s);
        if (in_anchor) {
            anchor.anchorText.push_back(s);
        }
    }

    enum get_url_return {
        success,
        no_url,
        failure
    };

    // parses URL from tag at ptr and stores it in s
    get_url_return get_url(char* ptr, const char* endPtr, std::string& s, bool is_embed = false) {
        char *start, *end = ptr;
        char cmp = (is_embed) ? 's' : 'h';
        while (ptr < endPtr) {
            if (*ptr == '>') {
                return no_url;
            }
            if (*ptr == cmp) {
                start = (is_embed) ? ptr + 4 : ptr + 5;  // distance until start of quote with URL

                if (start >= endPtr) {
                    return failure;
                }

                if (is_embed) {
                    if (*(ptr + 1) != 'r' && *(ptr + 2) != 'c' && (*ptr + 3) != '=') {
                        ptr++;
                        continue;
                    }
                } else {
                    if (*(ptr + 1) != 'r' && *(ptr + 2) != 'e' && (*ptr + 3) != 'f' && (*ptr + 4) != '=') {
                        ptr++;
                        continue;
                    }
                }

                if (*start != '"') {
                    ptr++;
                    continue;
                }

                // found URL location
                ptr = ++start;
                while (ptr < endPtr) {
                    if (*ptr == '"') {
                        if (ptr == start) {
                            return no_url;
                        }
                        end = ptr;
                        s = std::string(start, end);

                        // clean url
                        size_t special_char = s.find_first_of("&%#+;@");
                        s = s.substr(0, special_char);

                        if(s.length() < 4) {
                            return no_url;
                        }

                        if(s.substr(0, 4) != std::string("http")) {
                            return no_url;
                        }

                        if (s.size() > __UINT16_MAX__) {
                            return no_url;
                        }
                        return success;
                    }
                    ptr++;
                }
            }
            ptr++;
        }
        return failure;
    }

   public:
    // The constructor is given a buffer and length containing
    // presumed HTML.  It will parse the buffer, stripping out
    // all the HTML tags and producing the list of words in body,
    // words in title, and links found on the page.

    HtmlParser(const char* buffer, size_t length)  // YOUR CODE HERE
    {
        // YOUR CODE HERE
        // Outer loop: Looping through the start and end of the html first
        // Inner loop: We're iterating through each indivdual tags whenever we encounter them

        // Starting with the outer loop
        // We read everything line by line until we encounter an opening tag (<) plus whatever tag comes immediately after it
        // Once we read in the tag (it's only going to be the tag without any of the symbols so we can look it up in the table)
        // Then we're going to call the LookupPossibleTag func with the tag and see what the desired action is.
        // Depending on which action we need to take is,
        // (we're going to have a switch case) we're going to process the content within each tag accordingly.
        // There's going to be 8 switch cases.
        // Once we're finished processing what we need to do, we're going to hop back to the HtmlParser
        // We'll just switch case based on what we get from lookupPossibleTag
        // Once the current tag is process, we will continue reading in text until we hit the next tag symbol and we'll
        // Continue the process until we reach the end of the file.

        // reading everything

        char* sofWord = nullptr;  // start of word ptr
        char* eofWord = nullptr;  // end of word ptr

        char* tag_sofWord = nullptr;  // tag start of word ptr
        char* tag_eofWord = nullptr;  // tag end of word ptr

        // dummy variables for switch cases
        char* p = nullptr;
        std::string s1, s2;
        get_url_return ret;

        bool is_closing_tag = false;
        bool found_base = false;

        const char* endPtr = buffer + length;
        for (char* ptr = const_cast<char*>(buffer); ptr < endPtr; ptr++) {
            // Detect tag
            if (*ptr == '<') {
                // word break
                eofWord = ptr;

                // scan for tag name until '>', '/>', or ' '
                tag_sofWord = ++ptr;
                is_closing_tag = false;
                if (ptr != endPtr) {
                    if (*ptr == '/') {
                        tag_sofWord = ++ptr;
                        is_closing_tag = true;
                    } else if (*ptr == '!') {
                        if (ptr + 2 < endPtr) {
                            if (*(ptr + 1) == '-' && *(ptr + 2) == '-') {
                                tag_eofWord = ptr + 3;
                                goto commentskip;
                            }
                        }
                    }
                }

                while (ptr < endPtr) {
                    if (*ptr == '>' || *ptr == '/' || isspace(*ptr)) {
                        tag_eofWord = ptr;
                        break;
                    }
                    ptr++;
                }

                // edgecase for hitting end of file during scan
                if (ptr == endPtr) {
                    if (!sofWord) {
                        sofWord = eofWord;  // opening '<'
                    }
                    break;
                }

                // decide action based on tag name
                commentskip:
                DesiredAction result = LookupPossibleTag(tag_sofWord, tag_eofWord);

                // individually handle to reduce code duplication
                if (result == DesiredAction::OrdinaryText) {
                    if (!sofWord) {
                        sofWord = eofWord;  // opening '<'
                    }
                } else {
                    switch (result) {
                        case DesiredAction::Title:
                            // have to store word here before disabling
                            if (sofWord) {
                                store_word(sofWord, eofWord);
                                sofWord = nullptr;
                            }
                            // redirect pushed words to titleWords
                            if (is_closing_tag) {
                                push_words_to = &words;
                            } else {
                                push_words_to = &titleWords;
                            }
                            break;

                        case DesiredAction::Comment:
                            // parse until '-->'
                            while (ptr < endPtr) {
                                if (*ptr == '-' && ptr + 2 < endPtr) {
                                    if (*(ptr + 1) == '-' && *(ptr + 2) == '>') {
                                        break;
                                    }
                                }
                                ptr++;
                            }
                            break;

                        case DesiredAction::DiscardSection:
                            if (!is_closing_tag) {
                                // parse until found corresponding closing tag
                                s1 = std::string(tag_sofWord, tag_eofWord);
                                while (ptr < endPtr) {
                                    if (*ptr == '<') {
                                        p = ptr + 2 + s1.length();
                                        if (p >= endPtr) {
                                            // edgecase for hitting end of file
                                            if (!sofWord) {
                                                sofWord = eofWord;  // opening '<'
                                            }
                                            break;
                                        }
                                        if (*(p - 1 - s1.length()) != '/') {
                                            ptr++;
                                            continue;
                                        }
                                        s2 = std::string(p - s1.length(), p);
                                        if (s1 == s2) {
                                            break;
                                        }
                                    }
                                    ptr++;
                                }
                            }
                            break;

                        case DesiredAction::Anchor:
                            if (is_closing_tag) {
                                // have to store word here before disabling
                                if (sofWord) {
                                    store_word(sofWord, eofWord);
                                    sofWord = nullptr;
                                }
                                if (in_anchor) {
                                    links.push_back(anchor);
                                }
                                in_anchor = false;
                            } else {
                                // parse URL and if found set in_anchor to true and initialize it with the URL
                                ret = get_url(ptr, endPtr, s1);
                                if (ret == failure) {
                                    if (!sofWord) {
                                        sofWord = eofWord;  // opening '<'
                                    }
                                } else if (ret == success) {
                                    // have to store word here before disabling
                                    if (sofWord) {
                                        store_word(sofWord, eofWord);
                                        sofWord = nullptr;
                                    }
                                    if (in_anchor) {
                                        links.push_back(anchor);
                                    }
                                    in_anchor = true;
                                    anchor = Link(s1);
                                }
                            }
                            break;

                        case DesiredAction::Base:
                            if (!is_closing_tag && !found_base) {
                                // parse URL and place in base if found
                                ret = get_url(ptr, endPtr, s1);
                                if (ret == failure) {
                                    if (!sofWord) {
                                        sofWord = eofWord;  // opening '<'
                                    }
                                } else if (ret == success) {
                                    found_base = true;
                                    base = s1;
                                }
                            }
                            break;

                        case DesiredAction::Embed:
                            if (!is_closing_tag) {
                                // parse URL and if found add corresponding Link to links vector
                                ret = get_url(ptr, endPtr, s1, true);
                                if (ret == failure) {
                                    if (!sofWord) {
                                        sofWord = eofWord;  // opening '<'
                                    }
                                } else if (ret == success) {
                                    links.push_back(Link(s1));
                                }
                            }
                            break;

                        default:
                            break;
                    }
                    // parse until ptr is end of tag
                    while (ptr < endPtr && *ptr != '>') {
                        ptr++;
                    }

                    // store prev word
                    if (sofWord) {
                        store_word(sofWord, eofWord);
                        sofWord = nullptr;
                    }
                    continue;
                }
            }

            // word break -- save current word and update sofWord and eofWord ptrs
            if (isspace(*ptr)) {
                if (!sofWord) {
                    continue;
                }
                store_word(sofWord, ptr);
                sofWord = nullptr;
                continue;
            }

            // ordinary text
            if (!sofWord) {
                sofWord = ptr;
            }
        }

        // ended with word
        if (sofWord) {
            store_word(sofWord, endPtr);
        }
    }
};
