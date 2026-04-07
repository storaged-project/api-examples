GCC=@gcc
CFLAGS=-Wall -pedantic `pkg-config --cflags blockdev glib-2.0`
CLIBS=`pkg-config --libs blockdev glib-2.0` -lbd_utils -lm
UDISKS_CFLAGS=-Wall -pedantic `pkg-config --cflags udisks2 udisks2-lvm2 gio-2.0 glib-2.0`
UDISKS_CLIBS=`pkg-config --libs udisks2 udisks2-lvm2 gio-2.0 glib-2.0`
RM=@rm -rf

default: demo-1-libblockdev demo-1-progress demo-1-libudisks

clean:
	$(RM) demo-1-libblockdev demo-1-progress demo-1-libudisks

demo-1-libblockdev:
	$(GCC) $(CFLAGS) $(CLIBS) -o demo-1-libblockdev demo-1-libblockdev.c
demo-1-progress:
	$(GCC) $(CFLAGS) $(CLIBS) -o demo-1-progress demo-1-progress.c
demo-1-libudisks:
	$(GCC) $(UDISKS_CFLAGS) $(UDISKS_CLIBS) -o demo-1-libudisks demo-1-libudisks.c
