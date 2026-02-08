// WordCount.cpp
//
// Word count a set of files using a separate thread for each file.
//
// Compile with g++ --std=c++17 WordCount.cpp -pthread -o WordCount


#include <pthread.h>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

using namespace std;


// WordCount runs in a separate thread, counting words
// in an single file.
// 
// If a file can't be opened, ignore it, adding zero words to
// the count.
// 
// You are free to use either a memory-mapped implemenation
// or one that reads into a buffer.
// 
// When you create a thread, you get to pass a single
// argument as a void *, which the child thread can caste to
// anything meaningful it likes, e.g., a char *pathname.
//
// When WordCount finishes counting a file, the result needs
// to be added to the total.  There are two ways to do this.
// 
// (a) Add the count to a shared running total, locking
//     the resource with a pthread mutex while changing it.
//     (Most common.)
// 
// (b) Caste the count to a void * and return it as its exit
//     status, which can be retrieved in main with pthread_join
//     and added there to the total.


int TotalWords = 0;

// YOUR CODE HERE
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *WordCount( void *arg )
	{

	// YOUR CODE HERE

        return nullptr;
	}

// void* printArg(void* arg){
    
//     pthread_mutex_lock(&mutex);
//     TotalWords += 1;
//     cout << TotalWords << "\n";
//     pthread_mutex_unlock(&mutex);

//     return nullptr;
// }

// main() should iterate over the list of filenames given as
// arguments, creating a new thread running WordCount for each of the
// files.  Do not wait for a thread to finish before creating the
// next one.  Once all the threads complete, print total words.


int main( int argc, char** argv )
   {
   if ( argc <= 1 )
      {
      cerr <<	"Usage: WordCount <filenames>" << endl <<
					"Count the total number of word in the files." << endl <<
					"Invalid paths are ignored." << endl;
      return 1;
      }
      
    std::vector<pthread_t> threads;

    // step 1: iterate through input file args
    for (int i = 1; i < argc; ++i){

        char* curFile = argv[i];

        // step 2: create a thread that called WordCount with each filename
        pthread_t newThread;
        pthread_create(&newThread, NULL, WordCount, curFile);
        threads.emplace_back(newThread);
    }
    
    // step 3: join all threads from threads vector
    for (int i = 0; i < threads.size(); ++i){
        pthread_join(threads[i], NULL);
        cout << "Thread " << (int)i << " joined!" << "\n";
    } 

    // step 4: rejoice
    cout << "Total words = " << TotalWords << endl;
    return 0;

   }
