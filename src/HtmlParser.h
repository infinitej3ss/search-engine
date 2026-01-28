// HtmlParser.h
// Nicole Hamilton, nham@umich.edu

#pragma once

#include <vector>
#include <string>
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



class Link
   {
   public:
      std::string URL;
      std::vector< std::string > anchorText;

      Link( std::string URL ) : URL( URL )
         {
         }
   };


class HtmlParser
   {
   public:

      std::vector< std::string > words, titleWords;
      std::vector< Link > links;
      std::string base;

   private:
      // YOUR CODE HERE


   public:

      // The constructor is given a buffer and length containing
      // presumed HTML.  It will parse the buffer, stripping out
      // all the HTML tags and producing the list of words in body,
      // words in title, and links found on the page.

      HtmlParser( const char *buffer, size_t length ) // YOUR CODE HERE
         {
         // YOUR CODE HERE
         // Outer loop: Looping through the start and end of the html first
         // Inner loop: We're iterating through each indivdual tags whenever we encounter them
         //
         // Starting with the outer loop
         // We read everything line by line until we encounter an opening tag (<) plus whatever tag comes immediately after it
         // Once we read in the tag (it's only going to be the tag without any of the symbols so we can look it up in the table)
         // Then we're going to call the LookupPossibleTag func with the tag and see what the desired action is.
         // Depending on which action we need to take is,
         // (we're going to have a switch case) we're going to process the content within each tag accordingly.
         // There's going to be 8 switch cases.
         // Once we're finished processing what we need to do, we're going to hop back to the HtmlParser
         // We'll just switch case based on what we get from lookupPossibleTag
         // Once the current tag is process, we will continue reading in text until we hit thenext tag symbol and we'll
         // Continue the process until we reach the end of the file.


         

         }
   };
