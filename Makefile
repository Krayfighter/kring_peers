

all: kringp_daemon kringp_frontend

kringp_daemon: src/*
	gcc src/ipc.c src/daemon.c -o kringp_daemon

kringp_frontend: src/*
	gcc src/ipc.c src/frontend.c -Itermcodes -o kringp_frontend


