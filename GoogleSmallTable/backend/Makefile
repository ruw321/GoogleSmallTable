TARGETS = server master

all: $(TARGETS)

server: server.cc
	g++ $^ -I/usr/local/opt/openssl/include -L/usr/local/opt/openssl/lib -lcrypto -lpthread -g -o $@

master: master.cc
	g++ $< -lpthread -g -o $@
clean::
	rm -fv $(TARGETS) *~
