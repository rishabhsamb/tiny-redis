all : server client
.PHONY : all

server: server.o protocol.o
	g++ -o server.out server.o protocol.o

client: client.o protocol.o
	g++ -o client.out client.o protocol.o

server.o: server/server.cpp headers/conn.h headers/protocol.h
	g++ -c server/server.cpp

client.o: client/client.cpp headers/protocol.h
	g++ -c client/client.cpp

protocol.o: protocol/protocol.cpp headers/protocol.h
	g++ -c protocol/protocol.cpp
