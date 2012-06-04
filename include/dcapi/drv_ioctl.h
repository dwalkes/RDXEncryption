#ifndef _DRV_IOCTL_
#define _DRV_IOCTL_

#include "dcapi.h"
#include "..\sys\driver.h"
#include "dcconst.h"

typedef struct _vol_info {
	HANDLE    find;
	wchar_t   device[MAX_PATH];
	wchar_t   w32_device[MAX_PATH];
	dc_status status;

} vol_inf;

int dc_api dc_first_volume(vol_inf *info);
int dc_api dc_next_volume(vol_inf *info);

int  dc_api dc_is_old_runned();
int  dc_api dc_open_device();
void dc_api dc_close_device();
int  dc_api dc_get_version();
int  dc_api dc_clean_pass_cache();

int dc_api dc_get_boot_device(wchar_t *device);

int dc_api dc_add_password(dc_pass *password);
int dc_api dc_mount_volume(wchar_t *device, dc_pass *password, int flags);
int dc_api dc_start_encrypt(wchar_t *device, dc_pass *password, crypt_info *crypt);
int dc_api dc_start_decrypt(wchar_t *device, dc_pass *password);
int dc_api dc_start_re_encrypt(wchar_t *device, dc_pass *password, crypt_info *crypt);
int dc_api dc_mount_all(dc_pass *password, int *mounted, int flags);

int dc_api dc_start_format(wchar_t *device, dc_pass *password, crypt_info *crypt);
int dc_api dc_format_step(wchar_t *device, int wp_mode);
int dc_api dc_done_format(wchar_t *device);

int dc_api dc_change_password(
	  wchar_t *device, dc_pass *old_pass, dc_pass *new_pass
	  );

int dc_api dc_unmount_volume(wchar_t *device, int flags);
int dc_api dc_unmount_all();

int dc_api dc_enc_step(wchar_t *device, int wp_mode);
int dc_api dc_dec_step(wchar_t *device);
int dc_api dc_sync_enc_state(wchar_t *device);

int dc_api dc_get_device_status(wchar_t *device, dc_status *status);

int dc_api dc_add_seed(void *data, int size);
int dc_api dc_get_random(void *data, int size);

int dc_api dc_benchmark(crypt_info *crypt, dc_bench *info);

int dc_api dc_get_conf_flags(dc_conf *conf);
int dc_api dc_set_conf_flags(dc_conf *conf);

void dc_api dc_get_bsod();

int dc_api dc_backup_header(wchar_t *device, dc_pass *password, void *out);
int dc_api dc_restore_header(wchar_t *device, dc_pass *password, void *in);

int dc_api dc_lock_memory(void *data, u32 size);
int dc_api dc_unlock_memory(void *data);

#endif