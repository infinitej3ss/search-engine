CXX = g++ --std=c++17
CXX_debug = g++ --std=c++17 -g -O0

all:  Top10 HashTable HashBlob HashFile

Top10: Top10.cpp Common.h Common.cpp TopN.h TopN.cpp HashTable.h 
	$(CXX) Top10.cpp Common.cpp TopN.cpp -o Top10

HashTable: HashTable.cpp Common.h Common.cpp HashTable.h Timer.h
	$(CXX) HashTable.cpp Common.cpp -o HashTable

HashBlob:  HashBlob.cpp HashTable.h HashBlob.h Common.h Common.cpp Timer.h
	$(CXX) HashBlob.cpp Common.cpp -o HashBlob

HashFile:  HashFile.cpp HashTable.h HashBlob.h Common.h Timer.h
	$(CXX) HashFile.cpp Common.cpp -o HashFile


debug: Top10_debug

Top10_debug: Top10.cpp Common.h Common.cpp TopN.h TopN.cpp HashTable.h 
	$(CXX_debug) Top10.cpp Common.cpp TopN.cpp -o Top10_debug

clean:
	rm -f Top10 HashTable HashBlob HashFile Top10_debug
