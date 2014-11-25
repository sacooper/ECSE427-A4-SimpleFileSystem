CC=gcc
CCFLAGS=-Wall

all: libsfs.a ftest htest

ftest: sfs_ftest.c libsfs.a
	${CC} ${CCFLAGS} -o sfs_ftest sfs_ftest.c libsfs.a

htest: sfs_htest.c libsfs.a
	${CC} ${CCFLAGS} -o sfs_htest sfs_htest.c libsfs.a

libsfs.a: sfs_api.c sfs_api.h disk_emu.c disk_emu.h
	${CC} ${CCFLAGS} -c sfs_api.c
	${CC} ${CCFLAGS} -c disk_emu.c
	ar -cr libsfs.a sfs_api.o disk_emu.o

clean:
	rm *.o libsfs.a sfs_htest sfs_ftest
