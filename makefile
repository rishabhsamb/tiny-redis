all : server client
.PHONY : all

server: server.o protocol.o
	g++ -o server.out server.o protocol.o

client: client.o protocol.o
	g++ -o client.out client.o protocol.o

server.o: server.cpp protocol.h
	g++ -c server.cpp

client.o: client.cpp protocol.h
	g++ -c client.cpp

protocol.o: protocol.cpp protocol.h
	g++ -c protocol.cpp
