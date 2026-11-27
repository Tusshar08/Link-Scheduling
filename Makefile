CXX = g++
CXXFLAGS = -std=c++17 -pthread -O2 -Wall -Wextra -Isrc
LDFLAGS = -pthread

SERVER_SRC = $(addprefix src/,main.cpp config.cpp socket.cpp protocol.cpp file_io.cpp \
             server.cpp queue.cpp scheduling.cpp metrics.cpp)
CLIENT_SRC = $(addprefix src/,client.cpp config.cpp socket.cpp)
TEST_SRC   = tests/test_config.cpp src/config.cpp
HEADERS    = $(wildcard src/*.h)

.PHONY: all clean workload test

all: server client

server: $(SERVER_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)

client: $(CLIENT_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(CLIENT_SRC) $(LDFLAGS)

test_config: $(TEST_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_SRC) $(LDFLAGS)

test: test_config
	./test_config

workload:
	python3 scripts/gen_workload.py

clean:
	rm -f server client test_config
