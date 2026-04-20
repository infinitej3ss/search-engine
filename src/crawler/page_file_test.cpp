// ParsePage.cpp
// Nicole Hamilton, nham@umich.edu

// This is a simple demo of the HTML parser.  It reads a file
// containing presumed HTML into memory, creates an HtmlParser object
// to parse it, then prints out what it found.  By default, the output
// is a simplified HTML page representing the content.  Options allow
// for extracting only a portion of the contents.

#include <stdlib.h>

#include <cerrno>
#include <fstream>
#include <iostream>
#include <cstring>

#include "HtmlParser.h"
#include "page_data.h"

using namespace std;

enum class Options
   {
   PrintSimplePage,
   PrintWords,
   PrintTitleWords,
   PrintLinks,
   PrintBase,
   PrintOriginal,
   PrintStored
   };

Options option = Options::PrintSimplePage;

void PrintParse(PageData& parser) {
    switch (option) {
        case Options::PrintSimplePage:
            // Print a simplified page

            cout << "<!DOCTYPE html>" << endl
                 << "<html>" << endl;

            cout << "<title>" << endl;
            for (auto w : parser.titlewords)
                cout << w << " ";
            cout << endl
                 << "</title>" << endl
                 << endl;

            cout << "<body style=\"font-family:Verdana,Arial,Helvetica,sans-serif;font-size:10pt\">" << endl;

            cout << "<h2 style=\"font-family:Verdana,Arial,Helvetica,sans-serif;font-size:12pt; font-weight: bold\">" << endl
                 << "Title" << endl
                 << "</h2>" << endl;

            cout << "<p>" << endl;
            for (auto w : parser.titlewords)
                cout << w << " ";
            cout << endl
                 << "</p>" << endl
                 << endl;

            cout << "<h2 style=\"font-family:Verdana,Arial,Helvetica,sans-serif;font-size:12pt; font-weight: bold\">" << endl
                 << "Body text" << endl
                 << "</h2>" << endl;

            cout << "<p>" << endl;
            for (auto w : parser.words)
                cout << w << " ";
            cout << endl
                 << "</p>" << endl
                 << endl;

         cout << "</body>" << endl << "</html>" << endl;
         break;

      case Options::PrintWords:
         for ( auto w : parser.words )
            cout << w << endl;
         break;

      case Options::PrintTitleWords:
         for ( auto w : parser.titlewords )
            cout << w << endl;
         break;
    }
}

char *ReadFile( char *filename, size_t &fileSize )
   {
   // Read the file into memory.
   // You'll soon learn a much more efficient way to do this.

   // Attempt to Create an istream and seek to the end
   // to get the size.
   ifstream ifs( filename, ios::ate | ios::binary );
   if ( !ifs.is_open( ) )
      return nullptr;
   fileSize = ifs.tellg( );

   // Allocate a block of memory big enough to hold it.
   char *buffer = new char[ fileSize ];

   // Seek back to the beginning of the file, read it into
   // the buffer, then return the buffer.
   ifs.seekg( 0 );
   ifs.read( buffer, fileSize );
   return buffer;
   }


bool ValidOption( const char *o )
   {
   // Must be exactly one character.
   if ( !o[ 0 ] || o[ 1 ] )
      return false;

   switch ( o[ 0 ] )
      {
      case 'w':
         option = Options::PrintWords;
         break;
      case 't':
         option = Options::PrintTitleWords;
         break;
      case 'a':
         option = Options::PrintLinks;
         break;
      case 'b':
         option = Options::PrintBase;
         break;
      case 'o':
         option = Options::PrintOriginal;
         break;
      case 's':
         option = Options::PrintStored;
         break;
      default:
         return false;
      }
   return true;
   }


int main( int argc, char **argv )
   {
   if ( !( argc == 2 || argc > 2 && ValidOption( argv[ 1 ] ) ) )
      {
      cout << "Usage:  ParsePage [wtab] filename" << endl <<
              endl <<
              "By default, prints a simplified HTML page.  Options allow" << endl <<
              "for only a section of the content to be printed.  Only one" << endl <<
              "wtab option allowed at a time." << endl <<
              endl <<
              "   w  List the words, one per line." << endl <<
              "   t  List the title words, one per line." << endl <<
              "   a  List the links as \"URL ( anchor text )\", one per line." << endl <<
              "   b  Print any base that was found." << endl << 
              "   o  Print original document and write to page file" << endl <<
              "   s  Print stored document. Follow with document name." << endl;
      return 1;
      }

   if(option != Options::PrintStored) {
       size_t fileSize = 0;
       char* buffer = ReadFile(argv[1 + (argc == 3)], fileSize);
       if (!buffer) {
           cerr << "Could not open the file " << argv[1] << "." << endl;
           return 4;
       }

       HtmlParser parser(buffer, fileSize);

       PageData page_data;
       page_data.distance_from_seedlist = 0;
       page_data.url = "https://www.nytimes.com";
       page_data.titlewords = std::move(parser.titleWords);
       page_data.words = std::move(parser.words);
       

       if(write_page(0, page_data) == -1) {
         cout << "write failed\n";
         delete[] buffer;
         return 0;
       }

       if (option == Options::PrintOriginal) {
           option = Options::PrintSimplePage;
           PrintParse(page_data);
           write_page_file(0);
           delete[] buffer;
           return 0;
       }

       PageData page_data_swapped;
       page_data_swapped.distance_from_seedlist = 100;
       page_data_swapped.url = "https://www.timesny.com";
       page_data_swapped.titlewords = std::move(page_data.words);
       page_data_swapped.words = std::move(page_data.titlewords);

       option = Options::PrintSimplePage;
       PrintParse(page_data_swapped);

       if (write_page(0, page_data_swapped) == -1) {
           cout << "write failed\n";
           delete[] buffer;
           return 0;
       }

       write_page_file(0);

       delete[] buffer;
       return 0;
   }

   // print stored

   std::string file_name(argv[2]);

   // first load the page file
   if(load_page_file(file_name) == -1) {
      cout << "load failed for file: " << file_name << "\n";
      cout << "error: " << std::strerror(errno) << "\n";
      return 0;
   }

   // then call get_next_page with a PageData struct
   PageData page_data;
   if(get_next_page(page_data) == -1) {
      cout << "get page failed\n";
      return 0;
   }

   if (get_next_page(page_data) == -1) { // calling again gets the next entry, get_next_page will return -1 if there is not another entry
       cout << "get page failed\n";
       return 0;
   }

   option = Options::PrintSimplePage;
   PrintParse(page_data);
   
   return 0;
   }