/*
    *
    * DiskCryptor - open source partition encryption tool
    * Copyright (c) 2008 
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
#include <stdio.h>
#include "defines.h"
#include "devhook.h"
#include "misc.h"
#include "debug.h"
#include "storage.h"
#include "misc_mem.h"

#pragma pack (push, 1)

typedef struct _fat_bpb {
	s8	ignored[3];	/* Boot strap short or near jump */
	s8	system_id[8];	/* Name - can be used to special case
				   partition manager volumes */
	u8	bytes_per_sect[2];	/* bytes per logical sector */
	u8	sects_per_clust;/* sectors/cluster */
	u16	reserved_sects;	/* reserved sectors */
	u8	num_fats;	/* number of FATs */
	u16	dir_entries;	/* root directory entries */
	u8	short_sectors[2];	/* number of sectors */
	u8	media;		/* media code (unused) */
	u16	fat_length;	/* sectors/FAT */
	u16	secs_track;	/* sectors per track */
	u16	heads;		/* number of heads */
	u32	hidden;		/* hidden sectors (unused) */
	u32	long_sectors;	/* number of sectors (if short_sectors == 0) */

	/* The following fields are only used by FAT32 */
	u32	fat32_length;	/* sectors/FAT */
	u16	flags;		   /* bit 8: fat mirroring, low 4: active fat */
	u8	version[2];	   /* major, minor filesystem version */
	u32	root_cluster;	/* first cluster in root directory */
	u16	info_sector;	/* filesystem info sector */
	u16	backup_boot;	/* backup boot sector */
	u16	reserved2[6];	/* Unused */

} fat_bpb;

typedef struct exfat_bpb {
	u8	jmp_boot[3];	     /* boot strap short or near jump */
	u8	oem_id[8];		     /* oem-id */
	u8	unused0;		     /* 0x00... */
	u32	unused1[13];
	u64	start_sector;		 /* start sector of partition */
	u64	nr_sectors;		     /* number of sectors of partition */
	u32	fat_blocknr;		 /* start blocknr of FAT */
	u32	fat_block_counts;	 /* number of FAT blocks */
	u32	clus_blocknr;		 /* start blocknr of cluster */
	u32	total_clusters;		 /* number of total clusters */
	u32	rootdir_clusnr;		 /* start clusnr of rootdir */
	u32	serial_number;		 /* volume serial number */
	u8	xxxx01;			     /* ??? (0x00 or any value (?)) */
	u8	xxxx02;			     /* ??? (0x01 or 0x00 (?)) */
	u16	state;			     /* state of this volume */
	u8	blocksize_bits;		 /* bits of block size */
	u8	block_per_clus_bits; /* bits of blocks per cluster */
	u8	xxxx03;			     /* ??? (0x01 or 0x00 (?)) */
	u8	xxxx04;			     /* ??? (0x80 or any value (?)) */
	u8	allocated_percent;	 /* percentage of allocated space (?) */
	u8	xxxx05[397];		 /* ??? (0x00...) */
	u16	signature;		     /* 0xaa55 */

} exfat_bpb; 

#pragma pack (pop)

#define FAT_DIRENTRY_LENGTH 32

typedef struct _fs_info {
	int                     fs;
	u32                     bps;
	u64                     clusters;
	u64                     free_clus;
	u32                     clus_size;
	NTFS_VOLUME_DATA_BUFFER ntb;
	union {
		fat_bpb             bpb;
		exfat_bpb           ex_bpb;
	};

} fs_info;

#define FS_UNK   0
#define FS_FAT   1
#define FS_NTFS  2
#define FS_EXFAT 3

static int get_fs_info(HANDLE h_device, fs_info *info)
{
	u8                             buff[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 0x10];
	PFILE_FS_ATTRIBUTE_INFORMATION ainf = pv(buff);
	FILE_FS_SIZE_INFORMATION       sinf;	
	IO_STATUS_BLOCK                iosb;
	NTSTATUS                       status;
	int                            resl;
	u8                            *data;
	u64                            offset;

	data = NULL; offset = 0;
	do
	{
		status = ZwQueryVolumeInformationFile(
			h_device, &iosb, ainf, sizeof(buff), FileFsAttributeInformation);

		if (NT_SUCCESS(status) == FALSE) {
			resl = ST_ERROR; break;
		}

		ainf->FileSystemName[ainf->FileSystemNameLength >> 1] = 0;

		status = ZwQueryVolumeInformationFile(
			h_device, &iosb, &sinf, sizeof(sinf), FileFsSizeInformation);

		if (NT_SUCCESS(status) == FALSE) {
			resl = ST_ERROR; break;
		}

		info->fs        = FS_UNK;
		info->bps       = sinf.BytesPerSector;
		info->clusters  = sinf.TotalAllocationUnits.QuadPart;
		info->free_clus = sinf.AvailableAllocationUnits.QuadPart;
		info->clus_size = sinf.SectorsPerAllocationUnit * sinf.BytesPerSector;

		if ( (wcscmp(ainf->FileSystemName, L"FAT") == 0) || 
			 (wcscmp(ainf->FileSystemName, L"FAT32") == 0) )
		{
			info->fs = FS_FAT;
		}

		if (wcscmp(ainf->FileSystemName, L"exFAT") == 0) {
			info->fs = FS_EXFAT;
		}

		if ( (info->fs == FS_FAT) || (info->fs == FS_EXFAT) ) 
		{
			if ( (data = mm_alloc(info->bps, 0)) == NULL ) {
				resl = ST_ERROR; break;
			}
			status = ZwReadFile(
				h_device, NULL, NULL, NULL, &iosb, data, info->bps, pv(&offset), NULL);

			if (NT_SUCCESS(status) == FALSE) {				
				resl = ST_ERROR; break;
			}
			autocpy(&info->ex_bpb, data, min(sizeof(info->ex_bpb), info->bps));
		} 
		
		if (wcscmp(ainf->FileSystemName, L"NTFS") == 0) 
		{
			status = ZwFsControlFile(
				h_device, NULL, NULL, NULL, &iosb, 
				FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, &info->ntb, sizeof(info->ntb));

			if (NT_SUCCESS(status) == FALSE) {
				resl = ST_ERROR; break;
			}
			info->fs = FS_NTFS;
		}
		resl = ST_OK;
	} while (0);

	if (data != NULL) {
		mm_free(data);
	}
	return resl;
}

static 
u64 dc_make_continuous_file(
		   HANDLE h_device, HANDLE h_file, fs_info *fsi
		   )
{
	STARTING_VCN_INPUT_BUFFER  vcn;
	STARTING_LCN_INPUT_BUFFER  lcn;
	MOVE_FILE_DATA             mvd;
	IO_STATUS_BLOCK            iosb;
	NTSTATUS                   status;
	u64                        cluster, res = 0;
	RETRIEVAL_POINTERS_BUFFER *rpb;
	int                        i;
	struct {
		u64 start_lcn;
		u64 bitmap_size;
		u8  buffer[65536];
	} *data = NULL;	
	
	do
	{
		if (fsi->free_clus < 8) {
			break;
		}

		if ( (data = mm_alloc(sizeof(*data), 0)) == NULL ) {
			break;
		}
		/* find 8 free continuous clusters */
		lcn.StartingLcn.QuadPart = 32; cluster = 0;
		do
		{
			status = ZwFsControlFile(
				h_device, NULL, NULL, NULL, &iosb, 
				FSCTL_GET_VOLUME_BITMAP, &lcn, sizeof(lcn), data, sizeof(*data));

			if ( (NT_SUCCESS(status) == FALSE) && (status != STATUS_BUFFER_OVERFLOW) ) {				
				break;
			}

			for (i = 0; i < sizeof(data->buffer); i++)
			{
				if (data->buffer[i] == 0)
				{
					cluster = data->start_lcn + (i * 8);

					if ( (fsi->fs == FS_NTFS) && ((cluster + 8) >= d64(fsi->ntb.MftZoneStart.QuadPart)) && 
						 (cluster <= d64(fsi->ntb.MftZoneEnd.QuadPart)) )
					{
						cluster = 0; /* cluster in mft zone */
					} else {
						break;
					}
				}
			}

			lcn.StartingLcn.QuadPart += sizeof(data->buffer) * 8;
		} while (cluster == 0);

		/* move file to found clusters */
		mvd.FileHandle           = h_file;
		mvd.StartingVcn.QuadPart = 0;
		mvd.StartingLcn.QuadPart = cluster;
		mvd.ClusterCount         = ((DC_AREA_SIZE-1) / fsi->clus_size) + 1;

		status = ZwFsControlFile(
			h_device, NULL, NULL, NULL, &iosb, FSCTL_MOVE_FILE, &mvd, sizeof(mvd), NULL, 0);

		if (NT_SUCCESS(status) == FALSE) {		
			break;
		}

		/* check file position and continuity */
		vcn.StartingVcn.QuadPart = 0; rpb = pv(data);

		status = ZwFsControlFile(
			h_file, NULL, NULL, NULL, &iosb, 
			FSCTL_GET_RETRIEVAL_POINTERS, &vcn, sizeof(vcn), rpb, sizeof(*data));

		if (NT_SUCCESS(status) == FALSE) {
			break;
		}

		if ( (rpb->Extents[0].Lcn.QuadPart != cluster) || 
			 ( (rpb->ExtentCount != 1) && (rpb->Extents[0].NextVcn.QuadPart < mvd.ClusterCount) ) )
		{
			break;
		}
		res = cluster;
	} while (0);

	if (data != NULL) {
		mm_free(data);
	}
	return res;
}

HANDLE dc_open_storage_file(
		 wchar_t *dev_name, u32 disposition, ACCESS_MASK access
		 )
{
	UNICODE_STRING    u_name;
	OBJECT_ATTRIBUTES obj;
	IO_STATUS_BLOCK   iosb;
	wchar_t           f_name[MAX_PATH];
	HANDLE            h_file = NULL;
	
	_snwprintf(
		f_name, sizeof_w(f_name), L"%s\\$dcsys$", dev_name);

	f_name[sizeof_w(f_name) - 1] = 0;

	RtlInitUnicodeString(&u_name, f_name);

	InitializeObjectAttributes(
		&obj, &u_name, OBJ_KERNEL_HANDLE, NULL, NULL);

	ZwCreateFile(
		&h_file, access | SYNCHRONIZE, &obj, &iosb, NULL, 0, 0, disposition,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | FILE_WRITE_THROUGH, NULL, 0);
	
	return h_file;
}

static
void dc_storage_set_attributes(HANDLE h_file, u32 attributes)
{
	IO_STATUS_BLOCK        iosb;
	FILE_BASIC_INFORMATION binf;

	zeroauto(&binf, sizeof(binf));
	binf.FileAttributes = attributes;

	ZwSetInformationFile(
		h_file, &iosb, &binf, sizeof(binf), FileBasicInformation);
}

int dc_create_storage(dev_hook *hook, u64 *storage)
{
	IO_STATUS_BLOCK iosb;
	HANDLE          h_file;
	HANDLE          h_device;
	u64             cluster, offset;
	u16             state;
	fs_info         info;
	int             i, resl;
	void           *buff;
	NTSTATUS        status;
	
	state  = COMPRESSION_FORMAT_NONE;
	h_file = NULL; h_device = NULL; buff = NULL;
	do
	{
		/* open volume device */
		if ( (h_device = io_open_device(hook->dev_name)) == NULL ) {
			resl = ST_ACCESS_DENIED; break;
		}
		if (get_fs_info(h_device, &info) != ST_OK) {
			resl = ST_ERROR; break;
		}
		if (info.fs == FS_UNK) {
			resl = ST_CLUS_USED; break;
		}
		if ( (buff = mm_alloc(DC_AREA_SIZE, MEM_ZEROED)) == NULL ) {
			resl = ST_NOMEM; break;
		}
		/* delete old storage first */
		dc_delete_storage(hook);

		/* create new storage file */
		h_file = dc_open_storage_file(
			hook->dev_name, FILE_OPEN_IF, GENERIC_WRITE);

		if (h_file == NULL) {
			resl = ST_ACCESS_DENIED; break;
		}

		status = ZwWriteFile(
			h_file, NULL, NULL, NULL, &iosb, buff, DC_AREA_SIZE, NULL, NULL);

		if (NT_SUCCESS(status) == FALSE) {
			resl = ST_IO_ERROR; break;
		}

		if (info.fs == FS_NTFS)
		{
			ZwFsControlFile(
				h_file, NULL, NULL, NULL, &iosb, 
				FSCTL_SET_COMPRESSION, &state, sizeof(state), NULL, 0);

			dc_set_default_security(h_file);
		}

		dc_storage_set_attributes(
			h_file, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);

		/* try to make continuous file 10 times */
		for (i = 0; i < 10; i++)
		{
			if (cluster = dc_make_continuous_file(h_device, h_file, &info)) {
				break;
			}
			/* wait 0.2 sec */
			dc_delay(200);
		}

		if (cluster == 0) {
			resl = ST_CLUS_USED; break;
		}

		DbgMsg("cluster %0.8x%0.8x\n", p32(&cluster)[1], p32(&cluster)[0]);

		/* translate cluster number to volume offset */
		switch (info.fs)
		{
			case FS_FAT:
				{
					u32 fat_offset, fat_length;
					u32 root_max, data_offset;
					u32 root_offset;

					fat_offset = info.bpb.reserved_sects;
					fat_length = info.bpb.fat_length ? info.bpb.fat_length:info.bpb.fat32_length;
					root_offset = fat_offset + (info.bpb.num_fats * fat_length);

					if (root_max = info.bpb.dir_entries * FAT_DIRENTRY_LENGTH) {
						data_offset = root_offset + ((root_max - 1) / info.bps) + 1;
					} else {
						data_offset = root_offset;
					}
					offset = (d64(data_offset) * d64(info.bps)) + (cluster * d64(info.clus_size));
				}
			break;
			case FS_EXFAT:
				{
					offset = (cluster * d64(info.clus_size)) + (info.ex_bpb.clus_blocknr * info.bps);
				}
			break;
			case FS_NTFS:
				{
					offset = cluster * d64(info.clus_size);
				}
			break;
		}		
		DbgMsg("offset %0.8x%0.8x\n", p32(&offset)[1], p32(&offset)[0]);
		storage[0] = offset; resl = ST_OK;
	} while (0);

	if (h_file != NULL)   { ZwClose(h_file); }
	if (h_device != NULL) { ZwClose(h_device); }
	if (buff != NULL)     { mm_free(buff); }

	return resl;
}


void dc_delete_storage(dev_hook *hook)
{
	FILE_DISPOSITION_INFORMATION info;
	IO_STATUS_BLOCK              iosb;
	HANDLE                       h_file;

	h_file = dc_open_storage_file(
		hook->dev_name, FILE_OPEN, FILE_WRITE_ATTRIBUTES | DELETE);

	if (h_file != NULL)
	{
		info.DeleteFile = TRUE;

		dc_storage_set_attributes(h_file, FILE_ATTRIBUTE_NORMAL);
				
		ZwSetInformationFile(
			h_file, &iosb, &info, sizeof(info), FileDispositionInformation);

		ZwClose(h_file);
	}
}