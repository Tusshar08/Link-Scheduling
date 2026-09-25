CXX = g++
CXXFLAGS = -std=c++17 -pthread -O2 -Wall -Wextra
LDFLAGS = -pthread

SERVER_SRC = main.cpp config.cpp socket.cpp protocol.cpp file_io.cpp \
             server.cpp queue.cpp scheduling.cpp metrics.cpp
CLIENT_SRC = client.cpp config.cpp socket.cpp

.PHONY: all clean workload

all: server client

server: $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)

client: $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(CLIENT_SRC) $(LDFLAGS)

workload:
	python3 scripts/gen_workload.py

clean:
	rm -f server client
