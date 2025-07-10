CC = g++
CFLAGS = -Wall -O2 $(shell wx-config --cxxflags)
LDFLAGS = $(shell wx-config --libs) -libverbs

SRCS = StreamControl.cpp \
		LocalConf.cpp
OBJS = $(SRCS:.cpp=.o)

CLIENT_SRCS = client.cpp $(SRCS)
SERVER_SRCS = server.cpp $(SRCS)

CLIENT_OBJS = $(CLIENT_SRCS:.cpp=.o)
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)

all: client server

client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o client server

.PHONY: all clean