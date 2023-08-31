all:
	gcc test.c ringbuff.c -o ringbuff_test -lpthread

clean:
	rm -rf ringbuff_test ringbuff-* out.*
