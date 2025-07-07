

run: kring_peers
	./kring_peers

kring_peers: src/*
	gcc $(wildcard src/*.c) -o kring_peers


