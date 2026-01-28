#include <ctype.h>
#include <cstring>
#include <cassert>
#include "HtmlTags.h"

// name points to beginning of the possible HTML tag name.
// nameEnd points to one past last character.
// Comparison is case-insensitive.
// Use a binary search.
// If the name is found in the TagsRecognized table, return
// the corresponding action.
// If the name is not found, return OrdinaryText.

DesiredAction LookupPossibleTag( const char *name, const char *nameEnd )
   {
   // Your code here.
   // one of the two switches
   
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

   // switch(tag) {

   // }


   return DesiredAction::OrdinaryText;
   }