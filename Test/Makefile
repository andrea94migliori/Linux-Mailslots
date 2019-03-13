all: fifo_test msg_len_test read_blocking_test read_non_blocking_test write_blocking_test write_non_blocking_test

fifo_test: fifo_test.c
	gcc fifo_test.c -o fifo_test

msg_len_test: msg_len_test.c
	gcc msg_len_test.c -o msg_len_test

read_blocking_test: read_blocking_test.c
	gcc -pthread read_blocking_test.c -o read_blocking_test

read_non_blocking_test: read_non_blocking_test.c
	gcc -pthread read_non_blocking_test.c -o read_non_blocking_test

write_blocking_test: write_blocking_test.c
	gcc -pthread write_blocking_test.c -o write_blocking_test

write_non_blocking_test: write_non_blocking_test.c
	gcc -pthread write_non_blocking_test.c -o write_non_blocking_test

