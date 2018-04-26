GCC=		/usr/bin/gcc
CFLAGS=		-Wall -std=gnu99
TARGETS=	simplefs

simplefs: shell.o fs.o disk.o
	$(GCC) $(CFLAGS) shell.o fs.o disk.o -o simplefs

shell.o: shell.c
	$(GCC) $(CFLAGS) shell.c -c -o shell.o -g

fs.o: fs.c fs.h
	$(GCC) $(CFLAGS) fs.c -c -o fs.o -g

disk.o: disk.c disk.h
	$(GCC) $(CFLAGS) disk.c -c -o disk.o -g

clean:
	rm simplefs disk.o fs.o shell.o
