all:
	gcc -O3 -o simple simple.c city.c cuckoo.c -lrt -lpapi -Wall -Werror
	gcc -O3 -o glock glock.c city.c cuckoo.c -lrt -lpapi -Wall -Werror

clean:
	rm -f *.o simple glock
