ARCH=x86-64

all: brain
	@./brain -a ${ARCH} -c data.txt.old -o test.S
	@gcc test.S -o test

linux: brain
	@./brain -a ${ARCH} -l data.txt.old -o test.S
	@gcc -E test.S -o test.s
	@as test.s -o test.o
	@ld test.o -o test --strip-all
dbfi: brain
	@./brain -a ${ARCH} -l dbfi.b -o dbfi.S
	@gcc -E dbfi.S -o dbfi.s
	@as dbfi.s -o dbfi.o
	@ld dbfi.o -o dbfi --strip-all
brain: brain.c
	@gcc -g brain.c -o brain

abrain: abrain.S
