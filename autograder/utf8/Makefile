all: flip

flip: flip.cpp Utf8.cpp Utf8.h
	g++ --std=c++17 flip.cpp Utf8.cpp -o flip

debug: flip.cpp Utf8.cpp Utf8.h
	g++ --std=c++17 -g -O0 flip.cpp Utf8.cpp -o flip

clean:
	rm -f flip 
