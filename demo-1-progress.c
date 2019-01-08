/**
 * An example of a progress for e2fsck in libblockdev.
 */
#include <blockdev/blockdev.h>
#include <blockdev/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * A callback for a status report, passed to libblockdev.
 */
void prog_report(guint64 task_id, BDUtilsProgStatus status, guint8 completion, gchar *msg)
{
    static gint8 last_perc = -1;

    /*
       This function is called every time the running tool reports a
       progress. However, anything reported is rounded to whole percent by
       libblockdev. That means that in this function we can repeatedly see
       the same value: 42, 42, 42, 43, ... We don't want to spam the user's
       terminal with it, though, so we print every number just once.
    */
    if (msg == NULL && completion != last_perc) {
        printf("Progress: %d%%\n", completion);
        fflush(stdout);
        last_perc = completion;
    }

    if (msg != NULL) {
        printf("\n%s\n", msg);
    }
}

/**
 * Run fsck using a libblockdev's function.
 */
int fsck_blockdev(char *fs, char *fd_str)
{
    gint ret;
    g_autoptr(GError) error = NULL;
    BDPluginSpec fs_plugin = {BD_PLUGIN_FS, NULL};
    BDPluginSpec *plugins[] = {&fs_plugin, NULL};

    /* init */
    ret = bd_ensure_init (plugins, NULL, &error);
    if (!ret) {
        g_print("Error initializing libblockdev library: %s (%s, %d)\n",
             error->message, g_quark_to_string (error->domain), error->code);
        return 1;
    }
    bd_utils_init_prog_reporting(prog_report, &error);

    /* We can check if the progress reporting has been initialized or not
       at any time
    */
    if(!bd_utils_prog_reporting_initialized()) {
        g_print("Error, progress reporting is not initialized!\n");
        return 1;
    }

    /* run fsck */
    bd_fs_ext4_check (fs, NULL, &error);
    return 0;
}

void print_usage(char *name)
{
    fprintf(stderr, "Usage: %s device\n"
                    "  device   Path to ext4 device/image to fsck.\n",
            name);
}

int
main(int argc, char **argv)
{
    int opt;
    char *devicename;

    devicename = NULL;

    /* argparse */
    while ((opt = getopt(argc, argv, "")) != -1) {
        switch (opt) {
        default: /* '?' */
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* test for mandatory device arg */
    if (optind >= argc) {
        fprintf(stderr, "Expected adevice/image path.\n");
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    devicename = argv[optind];

    /* test for extra args */
    if (optind < argc-1) {
        fprintf(stderr, "Too many arguments.\n");
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    /* won't work without root */
    if (getuid () != 0) {
        g_print ("Requires to be run as root!\n");
        exit(EXIT_FAILURE);
    }

    /* run the fsck checks */
    fsck_blockdev(devicename, "1");
    printf("\n");

    return EXIT_SUCCESS;
}
