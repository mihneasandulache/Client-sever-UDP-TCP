CFLAGS = -Wall -g -Werror -Wno-error=unused-variable

PORT_SERVER = 12345

IP_SERVER = 127.0.0.1

all: server subscriber

server: server.cpp

subscriber: subscriber.cpp

.PHONY: clean run_server run_subscriber

run_server:
	./server ${PORT_SERVER}

run_subscriber:
	./subscriber ${ID_CLIENT} ${IP_SERVER} ${PORT_SERVER}

clean:
	rm -f server subscriber