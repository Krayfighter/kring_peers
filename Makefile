

all: kringp_daemon kringp_frontend

kringp_daemon: src/*
	gcc src/ipc.c src/daemon.c \
		-fsanitize=address -fsanitize=leak \
		-o kringp_daemon

kringp_frontend: src/*
	gcc src/ipc.c src/frontend.c \
		-fsanitize=address -fsanitize=leak \
		-o kringp_frontend


