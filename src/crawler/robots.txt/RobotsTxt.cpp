// RobotsTxt.cpp
//
// Nicole Hamilton, nham@umich.edu

// This is a starter file for an implementation of the RobotsTxt class
// described in RobotsTxt.h.
// 
// The RobotsTxt class can compile a robots.txt file and then determine whether
// access is allowed or disallowed for a given user agent and URL or path.  It
// will also extract any Sitemaps.

// This starter file a little more "paint by numbers" than I'd have liked but I
// wanted to keep the task reasonable.  And, it's only a suggestion. You do not
// have to use any of this code to pass the Autograder, so long as your
// implementation matches the interface definition in RobotsTxt.h.


#include "RobotsTxt.h"
#include <cctype>
#include <cstring>
#include <cstdlib>

using namespace std;


//
// Utilities
//

class ParsedUrlUtf8
   {
   private:
       Utf8 *pathBuffer;

   public:
      const Utf8  *CompleteUrl;
      Utf8        *Service,
                  *Host,
                  *Port,
                  *Path;

      ParsedUrlUtf8( const Utf8 *url )
         {
         // Assumes url points to static text but
         // does not check.

         CompleteUrl = url;

         pathBuffer = new Utf8[ strlen( ( const char * )url ) + 1 ];
         const Utf8 *f;
         Utf8 *t;
         for ( t = pathBuffer, f = url;  *t++ = *f++; )
            ;

         Service = pathBuffer;

         const Utf8 Colon = ':', Slash = '/';
         Utf8 *p;
         for ( p = pathBuffer;  *p && *p != Colon;  p++ )
            ;

         if ( *p )
            {
            // Mark the end of the Service.
            *p++ = 0;

            if (*p == Slash)
               p++;
            if (*p == Slash)
               p++;

            Host = p;

            for ( ;  *p && *p != Slash && *p != Colon;  p++ )
               ;

            if ( *p == Colon )
               {
               // Port specified.  Skip over the colon and
               // the port number.
               *p++ = 0;
               Port = p;
               for ( ; *p && *p != Slash;  p++ )
                  ;
               }
            else
               Port = p;

            if ( *p )
               // Mark the end of the Host and Port.
               *p++ = 0;

            // Whatever remains is the Path.
            Path = p;
            }
         else
            Host = Path = p;
         }

      ~ParsedUrlUtf8( )
         {
         delete [ ] pathBuffer;
         }
   };


int HexLiteralCharacter( char c )
   {
   // If c contains the Ascii code for a hex character, return the
   // binary value; otherwise, -1.

   int i;

   if ( '0' <= c && c <= '9' )
      i = c - '0';
   else
      if ( 'a' <= c && c <= 'f' )
         i = c - 'a' + 10;
      else
         if ( 'A' <= c && c <= 'F' )
            i = c - 'A' + 10;
         else
            i = -1;

   return i;
   }


Utf8 *UrlDecode( const Utf8 *path )
   {
   // Unencode any %xx encodings of characters in the path but
   // this variation leaves %2f "/" encodings unchanged.

   // (Unencoding can only shorten a string or leave it unchanged.
   // It never gets longer.)

   // Caller responsibility to delete [ ] the returned string.

   Utf8 *decode = new Utf8[ strlen( ( const char * )path ) + 1 ], *t = decode, c, d;

   while ( ( c = *path++ ) != 0 )
      if ( c == '%' )
         {
         c = *path;
         if ( c )
            {
            d = *++path;
            if ( d )
               {
               int i, j, hex;
               i = HexLiteralCharacter( c );
               j = HexLiteralCharacter( d );
               hex = ( i << 4 | j );
               if ( hex >= 0 && hex != '/' )
                  {
                  path++;
                  *t++ = ( Utf8 )hex;
                  }
               else
                  {
                  // If the two characters following the %
                  // aren't both hex digits, or if it's a %2f
                  // encoding of a /, treat as literal text.

                  *t++ = '%';
                  path--;
                  }
               }
            }
         }
      else
         *t++ = c;

   // Terminate the string.
   *t = 0;

   return decode;
   }



//
// Robots.txt syntax
//


// <robots.txt> ::= <Line> { <Line > }
// <Line>  ::= { <blank line> | '#" <abitrary text> | <Rule> | <Sitemap> } <LineEnd>
// <Rule> ::= <UserAgent> { <UserAgent> } <Directive> { <Directive> }
// <UserAgent> ::= "User-agent:" <name>
// <Directive> ::= <Permission> <path> | <CrawlDelay>
// <Permission> = "Allow:" | "Disallow:"
// <wildcard> ::= <a string that may contain *>
// <Crawl-delay> ::= "Crawl-delay:" <integer>
// <Sitemap> ::= "Sitemap:" <path>



//
// Tokenizing
//


const Utf8  UserAgentString[ ]   = "user-agent:",
            AllowString[]        = "allow:",
            DisallowString[ ]    = "disallow:",
            CrawlDelayString[ ]  = "crawl-delay:",
            SitemapString[ ]     = "sitemap:";

enum StatementType
   {
   InvalidStatement = 0,
   UserAgentStatement,
   AllowStatement,
   DisallowStatement,
   CrawlDelayStatement,
   SitemapStatement
   };



bool MatchKeyword( const Utf8 *keyword, const Utf8 **start, const Utf8 *bound )
   {
   // Try to match and consume this keyword from the input pointed to by
   // *start.  If there's a  match, advance the start pointer to the next input
   // character after the matched keyword and return true.
   //
   // keyword is null-terminated string already in lower-case to be matched
   // case-independent against the start of the input text.

   // YOUR CODE HERE.
    
    // start points into a long blob of text, and we're searching for keywords within that blob
    
    // get a copy of pointer
    const Utf8 *p = *start;
    
    // match all characters in keyword, consider edge cases like keyword is empty
    // do direct numeric comparision between p and bound
    // handle the case if we are running out of the input to check keyword
    while (*keyword != 0 && p < bound) {
        // Convert input to lowercase to compare with keyword
        Utf8 inputChar = *p;
        if (inputChar >= 'A' && inputChar <= 'Z') {
            inputChar = inputChar - 'A' + 'a';
        }
        
        // compare and mismatch, return false
        if (inputChar != *keyword) {
            return false;
        }
        p++;
        keyword++;
    }
        
    // This catches keyword longer than remaining input, implicit check
    if (*keyword != 0) {
        return false;
    }

    *start = p;
    return true;
}


StatementType Identify( const Utf8 **start, const Utf8 *bound )
   {
   // Recognize User-agent, Allow, Disallow, Crawl-delay and
   // Sitemap statements beginning at *start.  Reject blank lines,
   // comments and invalid lines as Invalid.  If a match is found,
   // the text is consumed and *start is updated  to just past the
   // match.


   // YOUR CODE HERE.
    const Utf8 *p = *start;

    // skip leading whitespace
    while (p < bound && (*p == ' ' || *p == '\t')) {
        p++;
    }

    // blank lines, comments, or end of input are invalid
    if (p >= bound || *p == '#' || *p == '\n' || *p == '\r') {
        return InvalidStatement;
    }

    // dispatch on first character to avoid trying all 5 keywords
    Utf8 fc = *p | 0x20;

    if (fc == 'u') {
        if (MatchKeyword(UserAgentString, &p, bound)) {
            *start = p;
            return UserAgentStatement;
        }
    } else if (fc == 'a') {
        if (MatchKeyword(AllowString, &p, bound)) {
            *start = p;
            return AllowStatement;
        }
    } else if (fc == 'd') {
        if (MatchKeyword(DisallowString, &p, bound)) {
            *start = p;
            return DisallowStatement;
        }
    } else if (fc == 'c') {
        if (MatchKeyword(CrawlDelayString, &p, bound)) {
            *start = p;
            return CrawlDelayStatement;
        }
    } else if (fc == 's') {
        if (MatchKeyword(SitemapString, &p, bound)) {
            *start = p;
            return SitemapStatement;
        }
    }

   return InvalidStatement;
   }


Utf8 *GetArgument( const Utf8 **start, const Utf8 *bound )
   {
   // Extract the argument from the input by skipping white space,
   // then taking anything not white space.  Return it as a string
   // on the heap.  Caller responsibility to delete.
   //
   // *start is updated to just past the last character consumed.

    const Utf8 *p = *start;

    // skip leading whitespace after the keyword
    while (p < bound && (*p == ' ' || *p == '\t')) {
        p++;
    }

    // find end of argument (stop at whitespace, newline, comment, or bound)
    const Utf8 *argStart = p;
    while (p < bound && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '#') {
        p++;
    }

    // copy argument to heap
    size_t len = p - argStart;
    Utf8 *arg = new Utf8[len + 1];
    memcpy(arg, argStart, len);
    arg[len] = 0;

    *start = p;
    return arg;
   }


const Utf8 *NextLine( const Utf8 **start, const Utf8 *bound )
   {
   // Consume everything up to the beginning of the next line,
   // updating *start, and return a pointer to it.

    const Utf8 *p = *start;

    // scan forward to end of current line
    while (p < bound && *p != '\n' && *p != '\r') {
        p++;
    }

    // skip past the line ending (\r\n, \r, or \n)
    if (p < bound) {
        if (*p == '\r') {
            p++;
            if (p < bound && *p == '\n') {
                p++;
            }
        } else {
            p++;
        }
    }

    *start = p;
    return *start;
   }



//
// Statement parsing and evaluation
//


StatementType GetStatement( const Utf8 **start, const Utf8 *bound, vector< string > &sitemap )
   {
   // Find the next valid User-agent, Allow, Disallow, or Crawl-delay
   // statement and return the statement type.  Return InvalidStatement if
   // none are found. At entry, assume *start is pointing at the beginning of
   // a line.
   //
   // Skip over any comments, blank lines, or invalid statements as
   // necessary.
   //
   // Sitemap statements are recognized here and the argument URL is added to
   // the sitemap vector, but otherwise skipped because they're global, and
   // don't affect how rules are parsed.

    // keep scanning lines until we find a valid statement or hit end
    while (*start < bound) {
        StatementType type = Identify(start, bound);

        if (type == SitemapStatement) {
            // construct string directly from input without intermediate allocation
            const Utf8 *sp = *start;
            while (sp < bound && (*sp == ' ' || *sp == '\t')) sp++;
            const Utf8 *argStart = sp;
            while (sp < bound && *sp != ' ' && *sp != '\t' &&
                   *sp != '\n' && *sp != '\r' && *sp != '#') sp++;
            sitemap.push_back(string((const char *)argStart, sp - argStart));
            *start = sp;
            NextLine(start, bound);
            continue;
        }

        if (type != InvalidStatement) {
            return type;
        }

        // skip invalid / blank / comment lines
        NextLine(start, bound);
    }

    return InvalidStatement;
   }


const Utf8 *FindSubstring( const Utf8 *a, const Utf8 *b )
   {
   // Look for an occurrence of string a in string b using case-independent
   // compare.  If a match is found, return a pointer to where in b.  If no
   // match is found, return nullptr.

    if (!a || !b) {
        return nullptr;
    }

    // try matching a at each position in b
    for (const Utf8 *p = b; *p; p++) {
        const Utf8 *pa = a, *pb = p;
        while (*pa && *pb) {
            Utf8 ca = *pa, cb = *pb;
            // convert both to lowercase for comparison
            if (ca >= 'A' && ca <= 'Z') ca = ca - 'A' + 'a';
            if (cb >= 'A' && cb <= 'Z') cb = cb - 'A' + 'a';
            if (ca != cb) break;
            pa++;
            pb++;
        }
        // if we exhausted a, we found a match
        if (*pa == 0) {
            return p;
        }
    }

   //Did not find a match anywhere.
   return nullptr;
   }


class UserAgent
   {
   public:
      Utf8 *name;
      bool matchAny;

      bool Match( const Utf8 *user )
         {
         // Return true if the name appears anywhere in the string
         // pointed to by user. Use case-independent compare.

         if (matchAny) {
             return true;
         }
         return FindSubstring(name, user) != nullptr;
         }

      UserAgent( const Utf8 **start, const Utf8 *bound )
         {
         // The name is the argument immediately following on
         // the input. Note if it's an * match-any wildcard..

         name = GetArgument(start, bound);
         // treat * or empty name as match-any
         matchAny = (name[0] == '*' && name[1] == 0) || name[0] == 0;
         }

      ~UserAgent( )
         {
         delete [ ] name;
         }
   };


class Wildcard
   {
   private:
      bool Match( const Utf8 *wildcard, const Utf8 *path )
         {
         // Return true if the path matches the wildcard.
         //
         // Assumes the original URL has been parsed, service, host,
         // port, and any initial slash have been stripped away.
         // path is what remains and has been URL decoded already.
         //
         // If with either wildcard or path is null, return false.

         if (!wildcard || !path) {
             return false;
         }

         while (*wildcard) {
             if (*wildcard == '*') {
                 // skip consecutive *'s
                 while (*wildcard == '*') {
                     wildcard++;
                 }

                 // * at end of wildcard matches everything remaining
                 if (*wildcard == 0) {
                     return true;
                 }

                 // *$ also matches everything to end
                 if (*wildcard == '$' && *(wildcard + 1) == 0) {
                     return true;
                 }

                 // try matching rest of wildcard at every position in path
                 while (*path) {
                     if (Match(wildcard, path)) {
                         return true;
                     }
                     path++;
                 }
                 // try with empty remaining path (for $ at end)
                 return Match(wildcard, path);
             } else if (*wildcard == '$' && *(wildcard + 1) == 0) {
                 // $ at end means path must also be at end
                 return *path == 0;
             } else {
                 // literal character must match exactly (case-sensitive)
                 if (*path != *wildcard) {
                     return false;
                 }
                 wildcard++;
                 path++;
             }
         }

         // wildcard exhausted, prefix match succeeds
         return true;
         }


   public:
      Utf8     *wildcard;   // URL-decoded version of the argument
      size_t   length,
               literalBytes;
      bool     endsInDollarSign;


      Wildcard( const Utf8 **start, const Utf8 *bound )
         {
         // Create a wildcard using the argument immediately
         // following in the input stream.
         //
         // 1. Get the argument.
         // 2. Discard any initial / and create a URL-decoded version.
         // 3. Count how many literal bytes there are.
         // 4. Note the length and whether it ends in $.
         // 5. Discard the original version as no longer needed.

         // parse argument and URL-decode in a single pass (one allocation)
         const Utf8 *p = *start;

         // skip whitespace after keyword
         while (p < bound && (*p == ' ' || *p == '\t')) p++;

         // find end of argument
         const Utf8 *argEnd = p;
         while (argEnd < bound && *argEnd != ' ' && *argEnd != '\t' &&
                *argEnd != '\n' && *argEnd != '\r' && *argEnd != '#') argEnd++;

         // skip initial /
         if (p < argEnd && *p == '/') p++;

         // allocate and URL-decode directly (decoding only shrinks)
         wildcard = new Utf8[argEnd - p + 1];
         Utf8 *t = wildcard;

         while (p < argEnd) {
             if (*p == '%' && p + 2 < argEnd) {
                 int i = HexLiteralCharacter(p[1]);
                 int j = HexLiteralCharacter(p[2]);
                 int hex = (i << 4 | j);
                 if (hex >= 0 && hex != '/') {
                     *t++ = (Utf8)hex;
                     p += 3;
                     continue;
                 }
             }
             *t++ = *p++;
         }
         *t = 0;

         length = t - wildcard;
         endsInDollarSign = length > 0 && wildcard[length - 1] == '$';

         // count literal bytes (everything except * and terminal $)
         literalBytes = 0;
         for (Utf8 *c = wildcard; *c; c++) {
             if (*c != '*') literalBytes++;
         }
         if (endsInDollarSign) literalBytes--;

         *start = argEnd;
         }


      ~Wildcard( )
         {
         delete [ ] wildcard;
         }


      bool Match( const Utf8 *path )
         {
         return Match( wildcard, path );
         }
   };

bool MoreSpecific( const Wildcard *a, const Wildcard *b )
   {
   // Return true if a is more specific than b.

   return a->literalBytes > b->literalBytes ||
         a->literalBytes == b->literalBytes &&
            a->endsInDollarSign && !b->endsInDollarSign;
   }


class Directive
   {
   public:
      StatementType type;
      Wildcard wildcard;

      Directive *Match( const Utf8 *path )
         {
         return wildcard.Match( path ) ? this : nullptr;
         }

      // If more than one directive applies based on the path, choose the one
      // which is
      //
      // 1. More specific, i.e., matches more characters in the URL, which we
      //    will measure as the number of literal (non-wildcard) bytes in the path.
      // 2. Ends in a $.
      // 3. Less restrictive, i.e., choose allow over disallow.
      // 4. Matches a literal User-Agent name, not just *.
      //
      // This means they can be sorted on a list when compiling a rule for a given
      // set of User-Agents; we can take the first one that matches.  But they
      // will still need to be compared if the match different User-agents.

      Directive( const StatementType type, const Utf8 **start, const Utf8 *bound ) :
            type( type ), wildcard( start, bound )
         {
         }

      ~Directive( )
         {
         }
   };


bool TakesPriority( const Directive *a, const Directive *b )
   {
   // Return true if a takes priority over b, meaning a is more specific
   // or less restrictivethan b.

   return  MoreSpecific( &a->wildcard, &b->wildcard ) ||
            a->type == AllowStatement && b->type == DisallowStatement;
   }


class Rule
   {
   public:
      vector< UserAgent * > UserAgents;
      vector< Directive * > Directives;
      int crawlDelay;

      Rule( ) : crawlDelay( 0 )
         {
         }

      ~Rule( )
         {
         // Delete the user agents.
         for ( auto u : UserAgents )
            delete u;

         // Delete the Directives.
         for ( auto d : Directives )
            delete d;
         }


      void AddUserAgent( const Utf8 **start, const Utf8 *bound )
         {
         // A UserAgent statement has been recognized. Create one with
         // the name specified next on the input and add it to the list
         // for this rule.

         UserAgent *u = new UserAgent( start, bound );
         UserAgents.push_back( u );
         }


      void AddDirective( const StatementType type,
            const Utf8 **start, const Utf8 *bound )
         {
         // An Allow or Disallow directive been found.  Create one with the
         // path specified as the next argument on the input and insertion sort
         // it into the list.

         Directive *d = new Directive(type, start, bound);

         // skip empty wildcards (empty Disallow/Allow has no effect)
         if (d->wildcard.length == 0) {
             delete d;
             return;
         }

         // insertion sort: find the right position by priority
         auto it = Directives.begin();
         while (it != Directives.end() && !TakesPriority(d, *it)) {
             it++;
         }
         Directives.insert(it, d);
         }


      void AddCrawlDelay( const Utf8 **start, const Utf8 *bound )
         {
         // A Crawl-delay statement has been found.  Read the argument
         // and attempt to convert to aninteger with atoi( ).  If it's
         // larger than current delay, use the new value.

         // parse integer directly from input without allocation
         const Utf8 *p = *start;
         while (p < bound && (*p == ' ' || *p == '\t')) p++;
         int delay = 0;
         while (p < bound && *p >= '0' && *p <= '9') {
             delay = delay * 10 + (*p - '0');
             p++;
         }
         if (delay > crawlDelay) crawlDelay = delay;
         *start = p;
         }


      Directive *Match( const Utf8 *user, const Utf8 *path, UserAgent **agent )
         {
         // A rule applies if it matches one of the user agents and there is a
         // directive that matches the path.  Since directives are sorted,
         // we can stop at the first match.
         //
         // If a match is found, return a pointer to the directive. Optionally
         // report which UserAgent was matched.

         // check if any user agent in this rule matches
         UserAgent *matchedAgent = nullptr;
         for (auto u : UserAgents) {
             if (u->Match(user)) {
                 matchedAgent = u;
                 break;
             }
         }

         if (!matchedAgent) {
             return nullptr;
         }

         if (agent) {
             *agent = matchedAgent;
         }

         // find first matching directive (sorted by priority)
         for (auto d : Directives) {
             if (d->wildcard.Match(path)) {
                 return d;
             }
         }

         return nullptr;
         }

   };


//
// RobotsTxt methods.
//


// Constructor to parse the robots.txt file contents into a
// set of rules and sitemaps.


RobotsTxt::RobotsTxt( const Utf8 *robotsTxt, const size_t length )
   {
   // Read the file one line at a time until the end, parsing
   // it into a vector of rules.

   const Utf8 *p = robotsTxt, *bound = p + length;
   StatementType type;

   // Skip over any byte order mark.

   if ( strncmp( ( const char * )Utf8BOMString,
         ( const char * )p, sizeof( Utf8BOMString) ) == 0 )
      p += sizeof( Utf8BOMString );

   // Skip over everything until a User-agent: is found.

   while ( p < bound &&
         ( type = GetStatement ( &p, bound, sitemap ) ) != UserAgentStatement )
      p = NextLine( &p, bound );

   while ( type == UserAgentStatement )
      {
      // 1. Create a new rule.
      // 2. Collect the user agents.
      // 3. Collect the directives.
      // 4. Add this rule to the list.

      // create a new rule for this group
      Rule *rule = new Rule();

      // collect all consecutive user-agent statements
      while (type == UserAgentStatement) {
          rule->AddUserAgent(&p, bound);
          NextLine(&p, bound);
          if (p < bound) {
              type = GetStatement(&p, bound, sitemap);
          } else {
              type = InvalidStatement;
          }
      }

      // collect all following directives
      while (type == AllowStatement || type == DisallowStatement ||
             type == CrawlDelayStatement) {
          if (type == CrawlDelayStatement) {
              rule->AddCrawlDelay(&p, bound);
          } else {
              rule->AddDirective(type, &p, bound);
          }
          NextLine(&p, bound);
          if (p < bound) {
              type = GetStatement(&p, bound, sitemap);
          } else {
              type = InvalidStatement;
          }
      }

      // add this rule to the list
      rules.push_back(rule);
      }
   }


RobotsTxt::~RobotsTxt( )
   {
   // Free up the rules.
   for ( auto r : rules )
      delete r;

   // The sitemap vector will clean up itself.
   }


Directive *RobotsTxt::FindDirective( const Utf8 *user, const Utf8 *path,
      int *crawlDelay, Rule **rule )
   {
   // Check the user and path against the rules in this robots.txt.  Return
   // the highest priority directive if one is found.
   //
   // Optionally report the containing rule and the highest value
   // crawl delay found among all the applicable rules.

   // 1. URL decode the path.
   // 2. Iterate over the rules, looking for one that best applies.

    Utf8 *decodedPath = UrlDecode(path);

    Directive *bestDirective = nullptr;
    Rule *bestRule = nullptr;
    bool bestMatchedWildcard = false;
    int maxCrawlDelay = 1; // default 1s

    for (auto r : rules) {
        // check if any user agent in this rule matches
        UserAgent *matchedAgent = nullptr;
        for (auto u : r->UserAgents) {
            if (u->Match(user)) {
                matchedAgent = u;
                break;
            }
        }

        if (!matchedAgent) continue;

        // find first matching directive (sorted by priority)
        Directive *d = nullptr;
        for (auto dir : r->Directives) {
            if (dir->wildcard.Match(decodedPath)) {
                d = dir;
                break;
            }
        }

        if (!d) continue;

        // this rule is applicable, track crawl delay
        if (r->crawlDelay > maxCrawlDelay) {
            maxCrawlDelay = r->crawlDelay;
        }

        bool isWildcard = matchedAgent->matchAny;

        if (!bestDirective) {
            bestDirective = d;
            bestRule = r;
            bestMatchedWildcard = isWildcard;
        } else if (bestMatchedWildcard && !isWildcard) {
            // literal agent name beats wildcard match
            bestDirective = d;
            bestRule = r;
            bestMatchedWildcard = isWildcard;
        } else if (bestMatchedWildcard == isWildcard &&
                   TakesPriority(d, bestDirective)) {
            bestDirective = d;
            bestRule = r;
        }
    }

    if (crawlDelay) {
        *crawlDelay = maxCrawlDelay;
    }
    if (rule) {
        *rule = bestRule;
    }

    delete[] decodedPath;
    return bestDirective;
   }


bool RobotsTxt::UrlAllowed( const Utf8 *user, const Utf8 *url,
      int *crawlDelay )
   {
   // Check a full URL against the rules in this robots.txt.
   // Return true if user is allowed to crawl the URL.
   // Optionally report any crawl delay.

   // Wrapper for PathAllowed that parses the URL first.

   ParsedUrlUtf8 ParsedUrlUtf8( url );
   return PathAllowed( user, ParsedUrlUtf8.Path, crawlDelay );
   }


bool RobotsTxt::PathAllowed( const Utf8 *user, const Utf8 *path,
      int *crawlDelay )
   {
   // Check the path portion of a URL against the rules in this
   // robots.txt.  Return true if user is allowed to crawl the path.
   // Optionally report any crawl delay. Skip over any initial /
   // in the path.
   //
   // Acts as wrapper for FindDirective to avoid making the definition
   // of a Directive part of the public interface in RobotsTxt.h.

   Directive *d = FindDirective( user, *path == '/' ? path + 1 : path, crawlDelay );
   return !d || d->type == AllowStatement;
   }


vector< string > RobotsTxt::Sitemap( )
   {
   return sitemap;
   }
