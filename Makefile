CFLAGS = -Wall -Wextra
LDLIBS = -lcurl -ljson-c -lncursesw

atm: atm.c
	gcc $(CFLAGS) atm.c -o atm $(LDLIBS)

clean:
	rm -f atm