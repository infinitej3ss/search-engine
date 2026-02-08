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
// Am I allowed to include this if it's not already there?
#include <thread>
#include <mutex>
#include <condition_variable>
#include <dirent.h>


using namespace std;

// workQueue
// pathVector
// condition variables for signaling
// declare mutexes - maybe one for each data structure - maybe just 2
// int for number of threads that are currently awake - 0 means all asleep

// DATA STRUCTURES AND GLOBALS
queue<string> workQueue;
vector<string> pathVector;
int ThreadCount = 0;
int awake;

// CONDITION VARS
condition_variable allAsleep;
condition_variable workAvailable;

// MUTEXES
mutex workQThread;
mutex pathVThread;


// we need to be careful that we aren't waiting on something that needs a mutex that we're already holding

// we only need one mutex for the workQueue and the number of awake threads
// another mutext for the path
	// one mutex for each data structure -- our orb!
	// the two things we're waiting on -- main waiting on the threads to all fall asleep, and the other is 
	// getWork waiting on addWork to add stuff to the queue



// Get the next path to be traversed from the work queue
// shared among all the threads, sleeping if the queue is
// empty. If this is the last thread going to sleep, signal
// that all the work is done.

string prefixPath(const string &parent, const string &child);
void AddPath( string path );

string GetWork( ) {
	string curTask;

	// CREATE UNIQUE LOCK FOR THE WORK QUEUE
	unique_lock<mutex> lock(workQThread);

	// WHILE THERE'S NO WORK AVAILABLE, GO TO SLEEP
	while (workQueue.empty()) {
		awake--;

		// SIGNAL TO MAIN THAT ALL ARE ASLEEP
		if (awake == 0) {
			allAsleep.notify_one();
		}

		// WAIT UNTIL WORK IS ADDED (THIS LOCKS THE MUTEX WHILE WAITING)
		workAvailable.wait(lock);
		
		// When we wake up, increment awake count
		awake++;
	}

	// FETCH THE NEXT TASK IN THE WORK QUEUE
	curTask = workQueue.front();
	workQueue.pop();

    // Add current path to path vector
    AddPath(curTask);

	// The unique_lock automatically unlocks when it goes out of scope
	return curTask;
}


void AddWork( string path ) {
	struct stat sb;
	
	// IGNORE NON-EXISTENT PATHS
	if (stat(path.c_str(), &sb) != 0) {
		return;
	}

	// LOCK THE DATA BEFORE TOUCHING THE SHARED DATA
	workQThread.lock();

	// ADD TO WORK QUEUE (BOTH FILES AND DIRECTORIES NEED TO BE TRAVERSED)
	workQueue.push(path);

	// SIGNAL THAT WORK IS AVAILABLE
	workAvailable.notify_one();

	// UNLOCK MUTEX
	workQThread.unlock();
}




void AddPath( string path ) {

	// LOCK THE PATH VECTOR MUTEX
	pathVThread.lock();

	// ADD FILES INTO THE PATH VECTOR
	pathVector.push_back(path);

	// UNLOCK MUTEX
	pathVThread.unlock();
}


bool DotName( const char *name ) {
   return name[ 0 ] == '.' &&
         ( name[ 1 ] == 0 || name[ 1 ] == '.' && name[ 2 ] == 0 );
}


// Traverse a path.  If it exists, add it to the list of paths that
// have been found.  If it's a directory, add any children other than
// . and .. to the work queue.  If it doesn't exist, ignore it.


void *Traverse( string pathname ) {
	struct stat sb;

	// CHECK IF THE PATH EXISTS
	if (stat(pathname.c_str(), &sb) != 0) {
		return nullptr;
	}

	// IF IT'S A DIRECTORY
	if (S_ISDIR(sb.st_mode)) {
		DIR *dir = opendir(pathname.c_str());
		if (dir == nullptr) {
			return nullptr; // CAN'T OPEN DIRECTORY
		}

		struct dirent *entry;
		while ((entry = readdir(dir)) != nullptr) {
			// SKIP . AND ..
			if (DotName(entry->d_name)) {
				continue;
			}

			// BUILD THE FULL PATH USING PREFIX PATH
			string fullPath = prefixPath(pathname, entry->d_name);

			// ADD TO WORK QUEUE
			AddWork(fullPath);
		}

		closedir(dir);
	}

	return nullptr;
}


// Each worker thread simply loops, grabbing the next item
// on the work queue and traversing it.


void *WorkerThread( void *arg ) {
	while ( true )
		Traverse( GetWork( ) );

	// Never reached.
	return nullptr;				
}


// CREATED A HELPER FUNCTION TO PREFIX THE PATHS
string prefixPath(const string &parent, const string &child) {
	if (parent.back() == '/') {
		return parent + child;
	}
	else {
		return parent + '/' + child;
	}
}


// main() should do the following.
// 
// 1. Initialize the locks.
// 2. Iterate over the argv pathnames, adding them to the work queue.
// 3. Create the specified number of workers.
// 4. Sleep until the work has finished.
// 5. Sort the paths found vector.
// 6  Print the list of paths.


int main( int argc, char **argv ) {
    if ( argc < 3  || ( ThreadCount = atoi( argv[ 1 ] ) ) == 0 ) {
      cerr <<	"Usage: Traverse <number of workers> <list of pathnames>" << endl 
				<< "Number of workers must be greater than 0." << endl 
				<< 	"Invalid paths are ignored." << endl;
      return 1;
    }

	// PARSE THREAD COUNT (already done in if statement above, but keeping for clarity)
	ThreadCount = atoi(argv[1]);

	// INIT 
	awake = ThreadCount;

	// ADD INITIAL PATHS FROM THE COMMAND LINE ARGS TO THE WORK QUEUE
	// Start at index 2 (skip program name and thread count)
	for (int i = 2; i < argc; i++) {
		AddWork(string(argv[i]));  // Use AddWork instead of directly pushing
	}

	// CREATE THE WORKER THREADS
	vector<pthread_t> threads(ThreadCount);
	for (int i = 0; i < ThreadCount; i++) {
		if (pthread_create(&threads[i], nullptr, WorkerThread, nullptr) != 0) {
			cerr << "Error creating thread " << i << endl;
			return 1;
		}
	}

	// WAIT UNTIL ALL THREADS ARE ASLEEP (ALL WORK IS DONE)
	unique_lock<mutex> lock(workQThread);
	while (awake > 0) {
		allAsleep.wait(lock);
	}
	lock.unlock();

	// NOW CANCEL/DETACH THE THREADS SINCE THEY'RE IN INFINITE LOOPS
	for (int i = 0; i < ThreadCount; i++) {
		pthread_cancel(threads[i]);
	}

	// SORT THE PATHS
	sort(pathVector.begin(), pathVector.end());

	// PRINT THE LIST OF PATHS
	for (const auto &path : pathVector) {
		cout << path << endl;
	}

	return 0;
}