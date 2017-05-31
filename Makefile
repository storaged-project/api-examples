GCC=@gcc
CFLAGS=-Wall -pedantic `pkg-config --cflags blockdev glib-2.0`
CLIBS=`pkg-config --libs blockdev glib-2.0` -lm
RM=@rm -rf

default: demo-1-libblockdev

clean:
	$(RM) demo-1-libblockdev

demo-1-libblockdev:
	$(GCC) $(CFLAGS) $(CLIBS) -o demo-1-libblockdev demo-1-libblockdev.c
