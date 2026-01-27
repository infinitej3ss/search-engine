// string.h
// 
// Starter file for a string template


#include <cstddef>   // for size_t
#include <iostream>  // for ostream

class string
   {
   private:
      char* str;
      size_t numChars;
      size_t limit;

   public: 
      // Element access (read-only, const)
      const char &operator[](size_t i) const {
         return str[i];
      }
   

      // Default Constructor
      // REQUIRES: Nothing
      // MODIFIES: *this
      // EFFECTS: Creates an empty string
      string( ) 
         {
            numChars = 0;                     // Initialize numChars
            limit = numChars + 1;             // Limit must be at least 1 to to store the null terminator
            str = new char[limit];            // Since str will outlive the function, memory must be allocated dynamically
            str[0] = '\0';                    // so we allocate memory on the heap, which survives past the constructor
         }                                    // Construct the empty string too so we don't get an error

      // string Literal / C string Constructor
      // REQUIRES: cstr is a null terminated C style string
      // MODIFIES: *this
      // EFFECTS: Creates a string with equivalent contents to cstr
      string( const char *cstr )
         {
            size_t length = 0;                // First count the length of cstr
            while (cstr[length] != '\0') {
               ++length;
            }

            numChars = length;
            limit = numChars + 1;
            str = new char[limit];            // Now I can create the string of the correct size

            for (size_t i = 0; i < numChars; i++) {
               str[i] = cstr[i];              // Copy all of the characters over
            }

            str[numChars] = '\0';             // Add the null terminator
         }

      // Size
      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns the number of characters in the string
      size_t size( ) const
         {
            return numChars; 
         }

      // C string Conversion
      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns a pointer to a null terminated C string of *this
      const char *cstr( ) const
         {
            return str;
         }

      // Iterator Begin
      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns a random access iterator to the start of the string
      const char *begin( ) const
         {
            return str;
         }

      // Iterator End
      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns a random access iterator to the end of the string
      const char *end( ) const
         {
            return str + numChars;
         }

      // Element Access
      // REQUIRES: 0 <= i < size()
      // MODIFIES: Allows modification of the i'th element
      // EFFECTS: Returns the i'th character of the string
      char &operator [ ]( size_t i )
         {
            return str[i];
         }

      // string Append
      // REQUIRES: Nothing
      // MODIFIES: *this
      // EFFECTS: Appends the contents of other to *this, resizing any
      //      memory at most once
      void operator+=( const string &other )
         {
            // make room for other just once
            size_t strSize = this->size();
            size_t otherSize = other.size();
            size_t newArrSize = strSize + otherSize + 1;

            char* newArray = new char[newArrSize];

            for (size_t i = 0; i < strSize; i++) {
               newArray[i] = str[i];
            }

            for (size_t i = 0; i < otherSize; i ++) {
               newArray[strSize + i] = other[i];
            }
            
            // Deallocate old memory
            delete[] str;

            numChars = strSize + otherSize;
            str = newArray;
            limit = newArrSize;

            str[numChars] = '\0';
         }

      // Push Back
      // REQUIRES: Nothing
      // MODIFIES: *this
      // EFFECTS: Appends c to the string
      void pushBack( char c )
         {
            // before writing, check if the buffer is full
            // if there's room, add the char
            // otherwise
            // 1. allocate a bigger buffer
            // 2. copy the old characters
            // 3. delete the old buffer
            // 4. update str and limit
            // 5. then add the new char

            size_t newSize = limit + 1;

            // If there's no room
            if (numChars + 1 == limit) {
               char* newArray = new char[newSize];

               // Copy the chars into the new array
               for (size_t i = 0; i < numChars + 1; i++) {
                  newArray[i] = str[i];
               }

               // Append c
               newArray[numChars] = c;

               // Deallocate the old mem
               delete[] str;

               // Set the newArray to be our new str;
               str = newArray;
               numChars++;
               limit = newSize;
               str[numChars] = '\0';

            }

            // Otherwise there is room
            else {
               str[numChars] = c;
               numChars++;
               str[numChars] = '\0';
            }
         }

      // Pop Back
      // REQUIRES: string is not empty
      // MODIFIES: *this
      // EFFECTS: Removes the last charater of the string
      void popBack( )
         {
            numChars--;
            str[numChars] = '\0';
         }

      // Equality Operator
      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns whether all the contents of *this
      //    and other are equal
      bool operator==( const string &other ) const
         {
            // Check to see if the size of the strings is the same
            if (numChars != other.numChars) {
               return false;
            }

            for (size_t i = 0; i < numChars; i++) {
               if (str[i] != other.str[i]) {
                  return false;
               }
            }

            return true;
         }

      // Not-Equality Operator
      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns whether at least one character differs between
      //    *this and other
      bool operator!=( const string &other ) const
         {
            if (numChars != other.numChars) {
               return true;
            }

            for (size_t i = 0; i < numChars; i++) {
               if (str[i] != other.str[i]) {
                  return true;
               }
            }

         return false;
      }

      // Less Than Operator
      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns whether *this is lexigraphically less than other
      bool operator<( const string &other ) const
         {
            size_t thisSize = numChars;
            size_t otherSize = other.numChars;
            size_t shortestSize = (thisSize < otherSize) ? thisSize : otherSize;

            for (size_t i = 0; i < shortestSize; i++) {
               // if the chars are the same, keep moving
               if (str[i] != other.str[i]) {

                  // The one with the smaller ascii value is smaller
                  // CORRECT THIS SECTION TOO!! - Done - Check the rest

                  return str[i] < other.str[i];
               }
            }

            return thisSize < otherSize;
         }

      // More efficient?
      // Greater Than Operator
      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns whether *this is lexigraphically greater than other
      bool operator>( const string &other ) const
         {
            size_t thisSize = numChars;
            size_t otherSize = other.numChars;
            size_t shortestSize = (thisSize < otherSize) ? thisSize : otherSize;

            for (size_t i = 0; i < shortestSize; i++) {
               // if the chars are the same, keep moving
               if (str[i] != other.str[i]) {

                  // The one with the smaller ascii value is smaller
                  return str[i] > other.str[i];
               }
            }
            return thisSize > otherSize;
         }

      // Less Than Or Equal Operator
      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns whether *this is lexigraphically less or equal to other
      bool operator<=( const string &other ) const
         {
            size_t thisSize = numChars;
            size_t otherSize = other.numChars;
            size_t shortestSize = (thisSize < otherSize) ? thisSize : otherSize;

            for (size_t i = 0; i < shortestSize; i++) {
               // if the chars are the same, keep moving
               if (str[i] != other.str[i]) {

                  // The one with the smaller ascii value is smaller
                  return str[i] < other.str[i];
               }
            }
            return thisSize <= otherSize;
         }

      // Greater Than Or Equal Operator
      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns whether *this is lexigraphically less or equal to other
      bool operator>=( const string &other ) const
         {
            size_t thisSize = numChars;
            size_t otherSize = other.numChars;
            size_t shortestSize = (thisSize < otherSize) ? thisSize : otherSize;

            for (size_t i = 0; i < shortestSize; i++) {
               // if the chars are the same, keep moving
               if (str[i] != other.str[i]) {

                  // The one with the smaller ascii value is smaller
                  return str[i] > other.str[i];
               }
            }

            return thisSize >= otherSize;
         }

   private:

   };

std::ostream &operator<<( std::ostream &os, const string &s )
   {
      for (size_t i = 0; i < s.size(); i++) {
         os << s[i];

      }
      return os;
   }