#include <udisks/udisks.h>
#include <stdio.h>
#include <unistd.h>

#define VGNAME "demo_1_libudisks"
#define SWAPNAME "swap"
#define SWAPLABEL "demoswap"
#define DATANAME "data"
#define DATALABEL "demodata"
#define PASSPHRASE "myshinylittlepassphrase"

#define GIB (guint64) (1024ULL * 1024 * 1024)

static GVariant *
no_options (void)
{
  return g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);
}

/* construct a UDisks2 block device object path from a device path like /dev/sda */
static gchar *
get_block_object_path (const gchar *device)
{
  const gchar *name = device;
  if (g_str_has_prefix (device, "/dev/"))
    name = device + 5;
  return g_strdup_printf ("/org/freedesktop/UDisks2/block_devices/%s", name);
}

/* find a block device for a given logical volume object path */
static UDisksBlock *
get_block_for_lv (UDisksClient *client, const gchar *lv_obj_path)
{
  GDBusObjectManager *manager = udisks_client_get_object_manager (client);
  g_autoptr(GList) objects = g_dbus_object_manager_get_objects (manager);
  UDisksBlock *ret_block = NULL;

  for (GList *l = objects; l != NULL; l = l->next) {
    UDisksObject *object = UDISKS_OBJECT (l->data);
    g_autoptr(UDisksBlockLVM2) block_lvm2 = udisks_object_get_block_lvm2 (object);
    if (block_lvm2 == NULL)
      continue;

    const gchar *lv_path = udisks_block_lvm2_get_logical_volume (block_lvm2);
    if (g_strcmp0 (lv_path, lv_obj_path) == 0) {
      ret_block = udisks_object_get_block (object);
      break;
    }
  }

  g_list_free_full (g_steal_pointer (&objects), g_object_unref);
  return ret_block;
}

/* create a logical volume and format it */
static gboolean
create_and_format_lv (UDisksClient *client,
                      UDisksVolumeGroup *vg,
                      const gchar *lv_name,
                      guint64 size,
                      const gchar *fmt,
                      const gchar *label,
                      const gchar *enc_passphrase,
                      GError **error)
{
  g_autofree gchar *lv_path = NULL;
  gboolean ret = FALSE;
  g_autoptr(UDisksBlock) block = NULL;

  ret = udisks_volume_group_call_create_plain_volume_sync (vg, lv_name, size,
                                                           no_options (),
                                                           &lv_path,
                                                           NULL, error);
  if (!ret) {
    g_prefix_error (error, "Error creating LV '%s': ", lv_name);
    return FALSE;
  }

  /* settle the client to make sure the new LV's block device is available */
  udisks_client_settle (client);

  /* find the block device for the newly created LV */
  block = get_block_for_lv (client, lv_path);
  if (block == NULL) {
    g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                 "Failed to find block device for LV '%s'", lv_name);
    return FALSE;
  }

  /* build format options */
  GVariantBuilder opts_builder;
  g_variant_builder_init (&opts_builder, G_VARIANT_TYPE ("a{sv}"));
  if (label != NULL)
    g_variant_builder_add (&opts_builder, "{sv}", "label", g_variant_new_string (label));
  if (enc_passphrase != NULL)
    g_variant_builder_add (&opts_builder, "{sv}", "encrypt.passphrase",
                           g_variant_new_string (enc_passphrase));

  ret = udisks_block_call_format_sync (block, fmt,
                                       g_variant_builder_end (&opts_builder),
                                       NULL, error);
  if (!ret) {
    g_prefix_error (error, "Error formatting LV '%s' as %s: ", lv_name, fmt);
    return FALSE;
  }

  return TRUE;
}

static gboolean
create_storage (UDisksClient *client, const gchar *disk1, const gchar *disk2,
                GError **error)
{
  gboolean ret;
  gchar input;

  g_print ("Going to wipe all signatures from '%s' and '%s'. Is this ok? [N/y]: ",
           disk1, disk2);
  input = getchar ();
  if (input != 'y' && input != 'Y') {
    g_print ("Aborted\n");
    return TRUE;
  }

  /* construct D-Bus object paths for the block devices */
  g_autofree gchar *disk1_path = get_block_object_path (disk1);
  g_autofree gchar *disk2_path = get_block_object_path (disk2);

  /* get the UDisks objects for the disks */
  g_autoptr(UDisksObject) disk1_obj = udisks_client_get_object (client, disk1_path);
  g_autoptr(UDisksObject) disk2_obj = udisks_client_get_object (client, disk2_path);

  if (disk1_obj == NULL || disk2_obj == NULL) {
    g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                 "Failed to find UDisks objects for the given disks");
    return FALSE;
  }

  g_autoptr(UDisksBlock) disk1_block = udisks_object_get_block (disk1_obj);
  g_autoptr(UDisksBlock) disk2_block = udisks_object_get_block (disk2_obj);

  if (disk1_block == NULL || disk2_block == NULL) {
    g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                 "Failed to get Block interfaces for the given disks");
    return FALSE;
  }

  /* wipe the existing metadata (if any) */
  ret = udisks_block_call_format_sync (disk1_block, "empty", no_options (),
                                       NULL, error);
  if (!ret) {
    g_prefix_error (error, "Error wiping %s: ", disk1);
    return FALSE;
  }

  ret = udisks_block_call_format_sync (disk2_block, "empty", no_options (),
                                       NULL, error);
  if (!ret) {
    g_prefix_error (error, "Error wiping %s: ", disk2);
    return FALSE;
  }

  /* make sure the LVM2 module is loaded -- LVM support is implemented in a
     module and might not be available by default */
  UDisksManager *manager = udisks_client_get_manager (client);
  ret = udisks_manager_call_enable_module_sync (manager, "lvm2", TRUE,
                                                NULL, error);
  if (!ret) {
    g_prefix_error (error, "Error enabling LVM2 module: ");
    return FALSE;
  }

  udisks_client_settle (client);

  /* get the LVM2 manager interface for VG operations */
  g_autoptr(UDisksObject) manager_obj = udisks_client_get_object (client,
                                          "/org/freedesktop/UDisks2/Manager");
  g_autoptr(UDisksManagerLVM2) lvm2_manager = udisks_object_get_manager_lvm2 (manager_obj);

  if (lvm2_manager == NULL) {
    g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                 "Failed to get LVM2 manager interface");
    return FALSE;
  }

  /* create a new VG using the two disks */
  const gchar *block_paths[] = {disk1_path, disk2_path, NULL};
  g_autofree gchar *vg_path = NULL;
  ret = udisks_manager_lvm2_call_volume_group_create_sync (lvm2_manager, VGNAME,
                                                           block_paths,
                                                           no_options (),
                                                           &vg_path,
                                                           NULL, error);
  if (!ret) {
    g_prefix_error (error, "Error creating VG: ");
    return FALSE;
  }

  udisks_client_settle (client);

  /* get the VG object to read its properties and create LVs on it */
  g_autoptr(UDisksObject) vg_obj = udisks_client_get_object (client, vg_path);
  g_autoptr(UDisksVolumeGroup) vg = udisks_object_get_volume_group (vg_obj);

  if (vg == NULL) {
    g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                 "Failed to get VolumeGroup interface for the newly created VG");
    return FALSE;
  }

  /* create a swap LV -- 10 % of available VG free space but at most 1 GiB */
  guint64 vg_free = udisks_volume_group_get_free_size (vg);
  guint64 swap_size = MIN (GIB, vg_free / 10);

  ret = create_and_format_lv (client, vg, SWAPNAME, swap_size, "swap", SWAPLABEL, NULL, error);
  if (!ret)
    return FALSE;

  /* re-read VG's free space -- it has changed after creating the swap LV */
  udisks_client_settle (client);
  vg_free = udisks_volume_group_get_free_size (vg);

  /* create an encrypted data LV using all remaining space, formatted as XFS */
  ret = create_and_format_lv (client, vg, DATANAME, vg_free, "xfs", DATALABEL, PASSPHRASE, error);
  if (!ret)
    return FALSE;

  return TRUE;
}

static gboolean
cleanup_storage (UDisksClient *client, const gchar *disk1, const gchar *disk2,
                 GError **error)
{
  gboolean ret;
  gchar input;

  g_print ("Going to remove all devices on '%s' and '%s'. Is this ok? [N/y]: ",
           disk1, disk2);
  input = getchar ();
  if (input != 'y' && input != 'Y') {
    g_print ("Aborted\n");
    return TRUE;
  }

  /* we need to go from leaf devices to the disks
     so first close the LUKS device on the data LV, then remove LVs, then VG */

  /* find the block device for the data LV and lock its LUKS */
  g_autofree gchar *data_lv_path = g_strdup_printf ("/org/freedesktop/UDisks2/lvm/%s/%s",
                                                    VGNAME, DATANAME);

  udisks_client_settle (client);

  g_autoptr(UDisksBlock) data_block = get_block_for_lv (client, data_lv_path);
  if (data_block == NULL) {
    g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                 "Failed to find block device for LV '%s'", DATANAME);
    return FALSE;
  }

  /* the block device has the Encrypted interface because it has LUKS */
  const gchar *data_block_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (data_block));
  g_autoptr(UDisksObject) data_block_obj = udisks_client_get_object (client, data_block_path);
  g_autoptr(UDisksEncrypted) encrypted = udisks_object_get_encrypted (data_block_obj);

  if (encrypted == NULL) {
    g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                 "Failed to get Encrypted interface for LV '%s'", DATANAME);
    return FALSE;
  }

  ret = udisks_encrypted_call_lock_sync (encrypted, no_options (), NULL, error);
  if (!ret) {
    g_prefix_error (error, "Error locking LUKS on %s: ", DATANAME);
    return FALSE;
  }

  /* now delete the data LV */
  g_autoptr(UDisksObject) data_lv_obj = udisks_client_get_object (client, data_lv_path);
  g_autoptr(UDisksLogicalVolume) data_lv = udisks_object_get_logical_volume (data_lv_obj);

  ret = udisks_logical_volume_call_delete_sync (data_lv, no_options (), NULL, error);
  if (!ret) {
    g_prefix_error (error, "Error deleting data LV: ");
    return FALSE;
  }

  /* delete the swap LV */
  g_autofree gchar *swap_lv_path = g_strdup_printf ("/org/freedesktop/UDisks2/lvm/%s/%s",
                                                    VGNAME, SWAPNAME);
  g_autoptr(UDisksObject) swap_lv_obj = udisks_client_get_object (client, swap_lv_path);
  g_autoptr(UDisksLogicalVolume) swap_lv = udisks_object_get_logical_volume (swap_lv_obj);

  ret = udisks_logical_volume_call_delete_sync (swap_lv, no_options (),
                                                NULL, error);
  if (!ret) {
    g_prefix_error (error, "Error deleting swap LV: ");
    return FALSE;
  }

  /* delete the VG -- wipe=TRUE removes PV metadata from the disks as well */
  g_autofree gchar *vg_path = g_strdup_printf ("/org/freedesktop/UDisks2/lvm/%s", VGNAME);
  g_autoptr(UDisksObject) vg_obj = udisks_client_get_object (client, vg_path);
  g_autoptr(UDisksVolumeGroup) vg = udisks_object_get_volume_group (vg_obj);

  ret = udisks_volume_group_call_delete_sync (vg, TRUE, no_options (), NULL, error);
  if (!ret) {
    g_prefix_error (error, "Error deleting VG: ");
    return FALSE;
  }

  return TRUE;
}


int main (int argc, char *argv[]) {
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  g_auto(GStrv) disks = NULL;
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
    { "cleanup", 0, 0, G_OPTION_ARG_NONE, &cleanup,
      "Cleanup mode -- remove previously created devices.", NULL },
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

  /* create a UDisks client -- this connects to the UDisks2 daemon over D-Bus
     and sets up an object manager to track all storage objects */
  g_autoptr(UDisksClient) client = udisks_client_new_sync (NULL, &error);
  if (client == NULL) {
    g_print ("Error connecting to the UDisks daemon: %s (%s, %d)\n",
             error->message, g_quark_to_string (error->domain), error->code);
    return 1;
  }

  if (cleanup) {
    ret = cleanup_storage (client, disks[0], disks[1], &error);
  } else {
    ret = create_storage (client, disks[0], disks[1], &error);
  }

  if (!ret) {
    g_print ("Error: %s (%s, %d)\n", error->message,
             g_quark_to_string (error->domain), error->code);
    return 1;
  }

  return 0;
}
