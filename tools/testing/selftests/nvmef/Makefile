all:

TEST_PROGS := nvmf_loop.sh

include ../lib.mk

check:
	./nvmf_loop.sh
	./nvmf_loop.sh -d -s
	./nvmf_loop.sh -b 4k

clean:

