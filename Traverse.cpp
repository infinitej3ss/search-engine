// Traverse.cpp
//
// Create and print a sorted list of all the paths specified
// on the command line, traversing any directories to add
// their contents using a pool of worker threads.  Ignore
// any non-existent paths.
// 
// Compile with g++ --std=c++17 Traverse.cpp -pthread -o Traverse


#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>

#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>


using namespace std;




// workQueue
// pathVector
// condition variables for signaling
// declare mutexes - maybe one for each data structure - maybe just 2
// int for number of threads that are currently awake - 0 means all asleep

// we need to be careful that we aren't waiting on something that needs a mutext that we're already holding

// we only need one mutex for the workQueue and the number of awake threads
// another mutext for the path
	// one mutex for each data structure -- our orb!
	// the two things we're waiting on -- main waiting on the threads to all fall asleep, and the other is 
	// getWork waiting on addWork to add stuff to the queue



// Get the next path to be traversed from the work queue
// shared among all the threads, sleeping if the queue is
// empty. If this is the last thread going to sleep, signal
// that all the work is done.


string GetWork( )
   {

	//		YOUR CODE HERE
	// Make sure that you fetch the next thing in the work queue - fijate como hacerlo porque seria igual como un fila normal
	// Find a way to scale how to keep track of the number of threads that are awake
	// Once they're all asleep, we send the signal to main --> just a condition var to send to main so that it knows to
	// continue --> make it a number
	// Check if it's 0 --> while not 0, we still have threads active and we wait, otherwise we continue to main --> this condition
	// is an if statement within main
	// The condition for checking for more work will be done in here so that we know whether or not we need to wake up threads

   }


// Add  work to the queue and signal that there's work available.


void AddWork( string path )
   {

	// all this does is literally just add things to the work queue 
	// and call add path 
	// and signal that work is available so our threads in getWork wake up to take care of it

   }


// Add a new path to the list all those that have been found,


void AddPath( string path )
	{

	// all we do is add the files that we've encountered into the vector
	
	}


bool DotName( const char *name )
   {
   return name[ 0 ] == '.' &&
         ( name[ 1 ] == 0 || name[ 1 ] == '.' && name[ 2 ] == 0 );
   }


// Traverse a path.  If it exists, add it to the list of paths that
// have been found.  If it's a directory, add any children other than
// . and .. to the work queue.  If it doesn't exist, ignore it.


void *Traverse( string pathname )
   {

	//		YOUR CODE HERE
	// Traverese is what's going to go through each directory to find the sub directories and the files
	// once we enounter something, we check if it's a dir or file and then call add work or add path
	// remember to prefix the child's name with the parent path name -- just use the string pathname + the child's name
	// might also need to add the forward slash for the proper strong concatenation

	}


// Each worker thread simply loops, grabbing the next item
// on the work queue and traversing it.


void *WorkerThread( void *arg )
	{
	while ( true )
		Traverse( GetWork( ) );

	// Never reached.
	return nullptr;				
	}


// main() should do the following.
// 
// 1. Initialize the locks.
// 2. Iterate over the argv pathnames, adding them to the work queue.
// 3. Create the specified number of workers.
// 4. Sleep until the work has finished.
// 5. Sort the paths found vector.
// 6  Print the list of paths.


int main( int argc, char **argv )
   {
   if ( argc < 3  || ( ThreadCount = atoi( argv[ 1 ] ) ) == 0 )
      {
      cerr <<	"Usage: Traverse <number of workers> <list of pathnames>" << endl <<
					"Number of workers must be greater than 0." << endl <<
					"Invalid paths are ignored." << endl;
      return 1;
      }


	//		YOUR CODE HERE
	// init all the things we get and parse what they are 
	// grab mutext for getwork queue
	// spawn the number of threads to param
	// set the number of awake threads to this number
	// we need to hold onto the mutex into main when init. our threads to mke sure they don't interefere with init
	// we do this 
	// then we wait on the conditional var for all threads being asleep 
	// gives up the mutex while we wait (by calling wait and wiating on the conditonal var) and we give 

	// sort and print

	return 0;
   }
