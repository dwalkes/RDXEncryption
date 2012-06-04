#ifndef _DEVHOOK_
#define _DEVHOOK_

#include <ntdddisk.h>
#include <ntddstor.h>
#include <ntddvol.h>
#include "defines.h"
#include "xts_fast.h"
#include "driver.h"
#include "data_wipe.h"

typedef enum _dc_pnp_state {

    NotStarted = 0,         // Not started yet
    Started,                // Device has received the START_DEVICE IRP
    Stopped,                // Device has received the STOP_DEVICE IRP
    Deleted                 // Device has received the REMOVE_DEVICE IRP

} dc_pnp_state;


typedef align16 struct _dev_hook
{
	u32            ext_type;    /* device extention type */
	PDEVICE_OBJECT orig_dev;
	PDEVICE_OBJECT hook_dev;
	PDEVICE_OBJECT pdo_dev;
	LIST_ENTRY     hooks_list;
	IO_REMOVE_LOCK remv_lock;

	wchar_t        dev_name[MAX_DEVICE + 1];

	xts_key        dsk_key;

	u32            flags;        /* device flags */
	u32            mnt_flags;    /* mount flags  */
	u32            disk_id;      /* unique volume id */
	u16            vf_version;   /* volume format version */

	KEVENT         paging_count_event;
	LONG           paging_count;
	
	u32            chg_count;    /* media changes counter */
	u32            chg_mount;    /* mount changes counter */
	u32            chg_last_v;   /* changes counter at last IOCTL_STORAGE_CHECK_VERIFY */

	u32            max_chunk;
	int            mnt_probed;
	int            mnt_probe_cnt;

	crypt_info     crypt;
	wipe_ctx       wp_ctx;

	u8            *tmp_buff;
	xts_key       *hdr_key;
	xts_key       *tmp_key;
	dc_header      tmp_header;

	u64            dsk_size; /* full device size */
	u64            use_size; /* user available part size */
	u32            bps;      /* bytes per sector */

	u64            tmp_size;	
	u64            stor_off;

	KMUTEX         busy_lock;
	KMUTEX         key_lock;

	int            sync_init_type;
	int            sync_init_status; /* sync mode init status */

	/* hook RW helper thread fields */
	KEVENT         rw_init_event;
	KEVENT         rw_work_event;
	HANDLE         rw_thread;
	LIST_ENTRY     rw_queue_head;
	KSPIN_LOCK     rw_queue_lock;
	
	/* fields for synchronous requests processing */
	LIST_ENTRY     sync_req_queue;
	LIST_ENTRY     sync_irp_queue;
	KSPIN_LOCK     sync_req_lock;
	KEVENT         sync_req_event;
	KEVENT         sync_enter_event;

	dc_pnp_state   pnp_state;
	dc_pnp_state   pnp_prev_state;

} dev_hook;

dev_hook *dc_find_hook(wchar_t *dev_name);

void dc_insert_hook(dev_hook *hook);
void dc_remove_hook(dev_hook *hook);

void dc_reference_hook(dev_hook *hook);
void dc_deref_hook(dev_hook *hook);

dev_hook *dc_first_hook();
dev_hook *dc_next_hook(dev_hook *hook);

#define dc_set_pnp_state(_hook_, _state_) \
	(_hook_)->pnp_prev_state = (_hook_)->pnp_state; \
	(_hook_)->pnp_state = (_state_);

#define dc_restore_pnp_state(_hook_) \
	(_hook_)->pnp_state = (_hook_)->pnp_prev_state;

void dc_init_devhook();



#endif