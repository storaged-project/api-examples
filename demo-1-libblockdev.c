#include <blockdev/blockdev.h>
#include <blockdev/crypto.h>
#include <blockdev/fs.h>
#include <blockdev/lvm.h>
#include <blockdev/swap.h>
#include <glib.h>
#include <stdio.h>
#include <unistd.h>

#define VGNAME "demo_1_libblockdev"
#define SWAPNAME "swap"
#define SWAPLABEL "demoswap"
#define DATANAME "data"
#define DATALABEL "demodata"
#define LUKSNAME "test-luks"
#define PASSPHRASE "passphrase"

#define MIB * 1024 * 1024
#define GIB * 1024 MIB


G_DEFINE_AUTOPTR_CLEANUP_FUNC(BDLVMVGdata, bd_lvm_vgdata_free)


static gboolean create_storage (const gchar **disks, GError **error) {
  gboolean ret = FALSE;
  gchar input;
  g_autoptr(BDLVMVGdata) vg_data = NULL;
  g_autofree gchar *lv_path = NULL;
  g_autofree gchar *luks_name = NULL;
  g_autofree gchar *luks_path = NULL;
  const gchar **disk_p = NULL;

  g_print ("Going to wipe all signatures from '%s' and '%s'. Is this ok? [N/y]: ", disks[0], disks[1]);
  input = getchar ();
  if (input != 'y' && input != 'Y') {
    g_print ("Aborted\n");
    return TRUE;
  }

  /* wipe given disks and create LVM PV "format" on them */
  for (disk_p = disks; *disk_p != NULL; disk_p++) {
    ret = bd_fs_wipe (*disk_p,
                      TRUE, /* wipe all signatures */
                      error);
    if (!ret) {
      /* wipefs fails when the device is already empty but we have a special
         error code for this and we can ignore it */
      if (g_error_matches (*error, BD_FS_ERROR, BD_FS_ERROR_NOFS)) {
        g_clear_error (error);
      } else {
        g_prefix_error (error, "Error when wiping %s: ", *disk_p);
        return FALSE;
      }
    }

    ret = bd_lvm_pvcreate (*disk_p,
                           0,    /* data alignment (first PE), 0 for default */
                           0,    /* size reserved for metadata, 0 for default */
                           NULL, /* extra options passed to the lvm tool */
                           error);
    if (!ret) {
      g_prefix_error (error, "Error when creating lvmpv format on %s: ", *disk_p);
      return FALSE;
    }
  }

  /* now create a VG using given disks */
  ret = bd_lvm_vgcreate (VGNAME, disks,
                         8 MIB,    /* PE size, 0 for default value */
                         NULL, /* extra options passed to the lvm tool */
                         error);
  if (!ret) {
    g_prefix_error (error, "Error when creating vg: ");
    return FALSE;
  }

  /* read information about the newly created VG */
  vg_data = bd_lvm_vginfo (VGNAME, error);
  if (vg_data == NULL) {
    g_prefix_error (error, "Error when getting info for the newly created vg: ");
    return FALSE;
  }

  /* create a linear LV for swap -- 10 % of available VG free space but at most
     1 GiB and run mkswap on it */
  ret = bd_lvm_lvcreate (VGNAME, SWAPNAME, MIN(1 GIB, vg_data->free / 10),
                         "linear",
                         NULL, /* list of PVs the newly created LV should use */
                         NULL, /* extra options passed to the lvm tool */
                         error);
  if (!ret) {
    g_prefix_error (error, "Error when creating swap lv: ");
    return FALSE;
  }

  lv_path = g_strdup_printf ("/dev/%s/%s", VGNAME, SWAPNAME);
  ret = bd_swap_mkswap (lv_path, SWAPLABEL,
                        NULL, /* extra options passed to the lvm tool */
                        error);
  if (!ret) {
    g_prefix_error (error, "Error when creating swap on %s: ", lv_path);
    return FALSE;
  }
  g_free (lv_path);

  /* re-read information about the VG -- its free space has changed */
  bd_lvm_vgdata_free (vg_data);
  vg_data = bd_lvm_vginfo (VGNAME, error);
  if (vg_data == NULL) {
    g_prefix_error (error, "Error when getting info for the newly created vg: ");
    return FALSE;
  }

  /* create a linear LV for data using all free space available; this LV will
     be encrypted (using cryptsetup) and will be formatted to XFS */
  ret = bd_lvm_lvcreate (VGNAME, DATANAME, vg_data->free, "linear",
                         NULL, /* list of PVs the newly created LV should use */
                         NULL, /* extra options passed to the lvm tool */
                         error);
  if (!ret) {
    g_prefix_error (error, "Error when creating data lv: ");
    return FALSE;
  }

  lv_path = g_strdup_printf ("/dev/%s/%s", VGNAME, DATANAME);
  ret = bd_crypto_luks_format (lv_path,
                               NULL, /* cipher specification, NULL for default value */
                               0,    /* key size in bits, 0 for default */
                               PASSPHRASE,
                               NULL, /* key file, NULL if not requested */
                               0,    /* minimum random data entropy */
                               error);
  if (!ret) {
    g_prefix_error (error, "Error when creating luks on %s: ", lv_path);
    return FALSE;
  }

  luks_name = g_strdup_printf ("%s-%s", LUKSNAME, DATANAME);
  ret = bd_crypto_luks_open (lv_path, luks_name, PASSPHRASE,
                             NULL,  /* key file, NULL if not requested */
                             FALSE, /* open as read-only */
                             error);
  if (!ret) {
    g_prefix_error (error, "Error when opening luks on %s: ", lv_path);
    return FALSE;
  }

  /* "bd_fs_xfs_mkfs" doesn't allow to specify label for the filesystem but it
     allows to specify "extra arguments" for the "mkfs.xfs" command so we will
     pass "-L demodata" to it to create XFS with "demodata" label
     "bd_fs_xfs_set_label" could be used instead of the extra argument
   */
  BDExtraArg label_arg = {"-L", DATALABEL};
  const BDExtraArg *extra_args[2] = {&label_arg, NULL};

  luks_path = g_strdup_printf ("/dev/mapper/%s", luks_name);
  ret = bd_fs_xfs_mkfs (luks_path, extra_args, error);

  if (!ret) {
    g_prefix_error (error, "Error when creating xfs on %s: ", luks_path);
    return FALSE;
  }

  return TRUE;

}

static gboolean cleanup_storage (const gchar **disks, GError **error) {
  gboolean ret = FALSE;
  gchar input;
  g_autofree gchar *path = NULL;
  const gchar **disk_p = NULL;

  g_print ("Going to remove all devices on '%s' and '%s'. Is this ok? [N/y]: ", disks[0], disks[1]);
  input = getchar ();
  if (input != 'y' && input != 'Y') {
    g_print ("Aborted\n");
    return TRUE;
  }

  /* remove LVs we've created before (and close the luks device first) */
  path = g_strdup_printf ("/dev/mapper/%s-%s", LUKSNAME, DATANAME);
  ret = bd_crypto_luks_close (path, error);
  if (!ret) {
    g_prefix_error (error, "Error when closing luks device %s: ", path);
    return FALSE;
  }

  ret = bd_lvm_lvremove (VGNAME, DATANAME,
                         FALSE, /* force remove */
                         NULL,  /* extra options passed to the lvm tool */
                         error);
  if (!ret) {
    g_prefix_error (error, "Error when removing data lv: ");
    return FALSE;
  }

  ret = bd_lvm_lvremove (VGNAME, SWAPNAME,
                         FALSE, /* force remove */
                         NULL,  /* extra options passed to the lvm tool */
                         error);
  if (!ret) {
    g_prefix_error (error, "Error when removing swap lv: ");
    return FALSE;
  }

  /* and now remove the VG */
  ret = bd_lvm_vgremove (VGNAME,
                         NULL, /* extra options passed to the lvm tool */
                         error);
  if (!ret) {
    g_prefix_error (error, "Error when removing vg: ");
    return FALSE;
  }

  /* remove LVM PV "format" from the disks */
  for (disk_p = disks; *disk_p != NULL; disk_p++) {
    ret = bd_lvm_pvremove (*disk_p,
                           NULL, /* extra options passed to the lvm tool */
                           error);
    if (!ret) {
      g_prefix_error (error, "Error when removing lvmpv format from %s: ", *disk_p);
      return FALSE;
    }
  }

  return TRUE;
}


int main (int argc, char *argv[]) {
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  g_auto(GStrv) disks = NULL;  /* GStrv is a typedef alias for gchar** */
  gboolean cleanup = FALSE;
  gboolean ret = FALSE;
  guint num_disks;

  if (getuid () != 0) {
    g_print ("Requires to be run as root!\n");
    return 1;
  }

  /* command line option parsing */
  GOptionEntry entries[] =
  {
    { "cleanup", 0, 0, G_OPTION_ARG_NONE, &cleanup, "Clenup mode -- remove previously created devices.", NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &disks, NULL, NULL },
    { NULL }
  };

  context = g_option_context_new ("DEVICE1 DEVICE2");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_print ("Option parsing failed: %s\n", error->message);
    return 1;
  }

  if (disks == NULL) {
    g_print ("Expected exactly 2 devices, got none.\n");
    return 1;
  }

  num_disks = g_strv_length (disks);
  if (num_disks != 2) {
    g_print ("Expected exactly 2 devices, got %d.\n", num_disks);
    return 1;
  }

  /* We need to specify what plugins we want before using libblockdev.
     This example needs crypto, fs, lvm and swap plugin.
     It is possible to have more plugins providing the same API, for example
     LVM plugin now has two implementations -- one using the lvm command line
     tool (libbd_lvm.so.2) and one using the dbus API (libbd_lvm-dbus.so.2).
     You can choose which plugin will be used as shown below for the lvm plugin.
   */
  BDPluginSpec crypto_plugin = {BD_PLUGIN_CRYPTO, NULL};
  BDPluginSpec fs_plugin = {BD_PLUGIN_FS, NULL};
  BDPluginSpec lvm_plugin = {BD_PLUGIN_LVM, "libbd_lvm.so.2"};
  BDPluginSpec swap_plugin = {BD_PLUGIN_SWAP, NULL};

  BDPluginSpec *plugins[] = {&crypto_plugin, &fs_plugin, &lvm_plugin,
                             &swap_plugin, NULL};

  /* initialize the library (if it isn't already initialized) and load
     all required modules
   */
  ret = bd_ensure_init (plugins, NULL, &error);
  if (!ret) {
    g_print ("Error initializing libblockdev library: %s (%s, %d)\n",
             error->message, g_quark_to_string (error->domain), error->code);
    return 1;
  }

  if (cleanup) {
    ret = cleanup_storage ((const gchar**) disks, &error);
    if (!ret) {
      g_print ("Error when cleaning up created devices: %s (%s, %d)\n",
               error->message, g_quark_to_string (error->domain), error->code);
      return 1;
    }
  } else {
    ret = create_storage ((const gchar**) disks, &error);
    if (!ret) {
      g_print ("Error when creating devices: %s (%s, %d)\n",
               error->message, g_quark_to_string (error->domain), error->code);
      return 1;
    }
  }

  return 0;
}
