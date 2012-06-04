/*
    *
    * DiskCryptor - open source partition encryption tool
    * Copyright (c) 2007-2008 
    * ntldr <ntldr@diskcryptor.net> PGP key ID - 0xC48251EB4F8E4E6E
    *

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <ntifs.h>
#include "defines.h"
#include "devhook.h"
#include "misc_irp.h"
#include "misc.h"
#include "driver.h"
#include "mount.h"
#include "enc_dec.h"
#include "mem_lock.h"
#include "debug.h"
#include "dump_hook.h"
#include "misc_mem.h"

typedef struct _pw_irp_ctx {
	WORK_QUEUE_ITEM  wrk_item;
	dev_hook        *hook;
	PIRP             irp;

} pw_irp_ctx;

NTSTATUS 
  dc_complete_irp(
    PIRP irp, NTSTATUS status, ULONG_PTR bytes
	)
{
	irp->IoStatus.Status      = status;
	irp->IoStatus.Information = bytes;

	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}

NTSTATUS dc_release_irp(dev_hook *hook, PIRP irp, NTSTATUS status)
{
	IoReleaseRemoveLock(&hook->remv_lock, irp);
	return dc_complete_irp(irp, status, 0);
}

NTSTATUS dc_forward_irp(dev_hook *hook, PIRP irp)
{
	NTSTATUS status;

	IoSkipCurrentIrpStackLocation(irp);
	status = IoCallDriver(hook->orig_dev, irp);
			
	IoReleaseRemoveLock(&hook->remv_lock, irp);
	return status;
}

static
NTSTATUS
  dc_sync_complete(
    PDEVICE_OBJECT dev_obj, PIRP irp, PKEVENT sync
	)
{
	KeSetEvent(sync, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS dc_forward_irp_sync(dev_hook *hook, PIRP irp)
{
	KEVENT   sync;
	NTSTATUS status;

	KeInitializeEvent(
		&sync, NotificationEvent, FALSE);

	IoCopyCurrentIrpStackLocationToNext(irp);

    IoSetCompletionRoutine(
		irp, dc_sync_complete, &sync, TRUE, TRUE, TRUE);

	status = IoCallDriver(hook->orig_dev, irp);

    if (status == STATUS_PENDING) {
		wait_object_infinity(&sync);
		status = irp->IoStatus.Status;
    }

	return status;
}


NTSTATUS
  dc_create_close_irp(
     PDEVICE_OBJECT dev_obj, PIRP irp
	 )
{
	PIO_STACK_LOCATION irp_sp;

	irp_sp = IoGetCurrentIrpStackLocation(irp);

	if (irp_sp->MajorFunction == IRP_MJ_CLOSE) {
		dc_sync_all_encs();
		dc_clean_locked_mem(irp_sp->FileObject);
	}

	return dc_complete_irp(irp, STATUS_SUCCESS, 0);
}

static 
NTSTATUS dc_process_power_irp(dev_hook *hook, PIRP irp)
{
	NTSTATUS           status;
	PIO_STACK_LOCATION irp_sp;
	int                no_pass = 0;

	irp_sp = IoGetCurrentIrpStackLocation(irp);

	if ( (irp_sp->MinorFunction == IRP_MN_SET_POWER) && 
		 (irp_sp->Parameters.Power.Type == SystemPowerState) )
	{
		wait_object_infinity(&hook->busy_lock);

		if (irp_sp->Parameters.Power.State.SystemState == PowerSystemHibernate)
		{
			/* prevent device encryption to sync device and memory state */
			hook->flags |= F_PREVENT_ENC;
			dc_send_sync_packet(hook->dev_name, S_OP_SYNC, 0);
		}

		if (irp_sp->Parameters.Power.State.SystemState == PowerSystemWorking) {
			/* allow encryption requests */
			hook->flags &= ~F_PREVENT_ENC;
		}

		KeReleaseMutex(&hook->busy_lock, FALSE);
	}

	if ( (irp_sp->MinorFunction == IRP_MN_QUERY_POWER) && 
		 (irp_sp->Parameters.Power.Type == SystemPowerState) &&
		 (irp_sp->Parameters.Power.State.SystemState == PowerSystemHibernate) )
	{
		if ( (dc_os_type != OS_VISTA) && (dump_hibernate_action() != 0) ) {
			PoStartNextPowerIrp(irp);
			status  = dc_complete_irp(irp, STATUS_UNSUCCESSFUL, 0);
			no_pass = 1;
		}
	}

	if (no_pass == 0) {
		PoStartNextPowerIrp(irp);
		IoSkipCurrentIrpStackLocation(irp);
		status = PoCallDriver(hook->orig_dev, irp);
	}

	IoReleaseRemoveLock(&hook->remv_lock, irp);

	return status;
}

static void dc_power_irp_worker(pw_irp_ctx *pwc)
{
	dc_process_power_irp(pwc->hook, pwc->irp);
	mm_free(pwc);
}

NTSTATUS dc_power_irp(dev_hook *hook, PIRP irp)
{
	pw_irp_ctx *pwc;

	if (KeGetCurrentIrql() == PASSIVE_LEVEL) {
		return dc_process_power_irp(hook, irp);
	}

	if ( (pwc = mm_alloc(sizeof(pw_irp_ctx), 0)) == NULL )
	{
		PoStartNextPowerIrp(irp);		
		return dc_release_irp(hook, irp, STATUS_INSUFFICIENT_RESOURCES);
	}

	pwc->hook = hook;
	pwc->irp  = irp;

	IoMarkIrpPending(irp);

	ExInitializeWorkItem(&pwc->wrk_item, dc_power_irp_worker, pwc);
	ExQueueWorkItem(&pwc->wrk_item, DelayedWorkQueue);

	return STATUS_PENDING;
}
