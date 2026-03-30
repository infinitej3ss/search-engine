#include <ctype.h>
#include <cstring>
#include <string>
#include <cassert>
#include "HtmlTags.h"
#include <unordered_map>

// name points to beginning of the possible HTML tag name.
// nameEnd points to one past last character.
// Comparison is case-insensitive.
// Use a binary search.
// If the name is found in the TagsRecognized table, return
// the corresponding action.
// If the name is not found, return OrdinaryText.

std::unordered_map<std::string, DesiredAction> action = {
   //{"", OrdinaryText},
   {"title", DesiredAction::Title},
   {"!--", DesiredAction::Comment},
   {"style", DesiredAction::DiscardSection},
   {"script", DesiredAction::DiscardSection},
   {"svg", DesiredAction::DiscardSection},
   {"a", DesiredAction::Anchor},
   {"base", DesiredAction::Base},
   {"embed", DesiredAction::Embed}
};

DesiredAction LookupPossibleTag( const char *name, const char *nameEnd )
   {
   // BST IMPLEMENTATION
   int low = 0;
   int high = NumberOfTags - 1;

   while (low <= high) {
      int mid = (low + high) / 2;

      // TAKE THE TAG AT THE MIDDLE OF THE TABLE
      const char* tableTag = TagsRecognized[mid].Tag;
      int i = 0;
      int comp = 0;

      while ((name + i) < nameEnd && tableTag[i] != '\0') {
         char c1 = tolower((unsigned char)name[i]);
         char c2 = tolower((unsigned char)tableTag[i]);

         // INPUT TAG IS SMALLER
         if (c1 < c2) {
            comp = -1;
            break;
         }

         // INPUT TAG IS LARGER
         if (c1 > c2) {
            comp = 1;
            break;
         }

         i++;
      }

      // IF TAGS ARE EQUAL, RETURN THE ACTION
      if (comp == 0) {
         if ((name + i) == nameEnd && tableTag[i] == '\0') {
            return TagsRecognized[mid].Action;
         }
         else if ((name + i) == nameEnd) {
            comp = -1;
         }
         else {
            comp = 1;
         }
      }

      // RESET THE SEARCH
      if (comp < 0) {
         high = mid - 1;
      }

      if (comp > 0) {
         low = mid + 1;
      }
   }
   return DesiredAction::OrdinaryText;
}