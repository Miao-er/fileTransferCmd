CC = g++
CFLAGS = -Wall  $(shell wx-config --cxxflags)
LDFLAGS = $(shell wx-config --libs) -libverbs

SRCS = StreamControl.cpp \
		LocalConf.cpp
OBJS = $(SRCS:.cpp=.o)

CLIENT_SRCS = client.cpp $(SRCS)
SERVER_SRCS = server.cpp $(SRCS)
SWITCH_SRCS = switch.cpp
MASTER_SRCS = master.cpp

CLIENT_OBJS = $(CLIENT_SRCS:.cpp=.o)
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)
SWITCH_OBJS = $(SWITCH_SRCS:.cpp=.o)
MASTER_OBJS = $(MASTER_SRCS:.cpp=.o)
all: client server switch master

client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

switch: $(SWITCH_OBJS)
	$(CC) -o $@ $^

master: $(MASTER_OBJS)
	$(CC) -o $@ $^

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o client server switch master

.PHONY: all clean
