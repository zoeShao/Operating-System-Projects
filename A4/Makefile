
CFLAGS=-std=gnu99 -Wall -g

all : ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore ext2_checker

ext2_mkdir : ext2_mkdir.o ext2_utils.o
	gcc $(CFLAGS) -o ext2_mkdir $^

ext2_cp : ext2_cp.o ext2_utils.o
	gcc $(CFLAGS) -o ext2_cp $^

ext2_ln : ext2_ln.o ext2_utils.o
	gcc $(CFLAGS) -o ext2_ln $^

ext2_rm : ext2_rm.o ext2_utils.o
	gcc $(CFLAGS) -o ext2_rm $^

ext2_restore : ext2_restore.o ext2_utils.o
	gcc $(CFLAGS) -o ext2_restore $^

ext2_checker : ext2_checker.o ext2_utils.o
	gcc $(CFLAGS) -o ext2_checker $^

%.o : %.c ext2.h ext2_utils.h
	gcc $(CFLAGS) -c $<

clean : 
	rm -f *.o all *~ 
