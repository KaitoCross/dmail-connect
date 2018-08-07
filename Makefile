all: dmail-connect


dmail-connect:
	g++ main.cpp SignalHandling.cpp -o $@ -std=c++11 -lpthread

clean:
	rm -rf dmail-connect

