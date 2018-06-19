CFLAGS ?= -O2 -Wall -fstack-protector-strong

libcapsicumizer.so: Makefile capsicumizer.c
	$(CC) -o libcapsicumizer.so -std=c99 -shared -fPIC -lelf $(CFLAGS) capsicumizer.c

demo: Makefile demo.c libcapsicumizer.so
	$(CC) -o demo -L`pwd` -lcapsicumizer -Wl,-rpath,`pwd` `pkg-config --cflags --libs gtk+-3.0` -fPIE -fpie $(CFLAGS) demo.c
