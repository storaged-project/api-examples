GCC=@gcc
CFLAGS=-Wall -pedantic `pkg-config --cflags blockdev glib-2.0`
CLIBS=`pkg-config --libs blockdev glib-2.0` -lbd_utils -lm
RM=@rm -rf

default: demo-1-libblockdev demo-1-progress

clean:
	$(RM) demo-1-libblockdev demo-1-progress

demo-1-libblockdev:
	$(GCC) $(CFLAGS) $(CLIBS) -o demo-1-libblockdev demo-1-libblockdev.c
demo-1-progress:
	$(GCC) $(CFLAGS) $(CLIBS) -o demo-1-progress demo-1-progress.c
