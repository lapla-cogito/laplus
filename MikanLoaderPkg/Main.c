#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DiskIo2.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/FileInfo.h>
#include "loader_internal.h"

#define INFO 0
#define ERROR 1

void Halt(void) { while (1) __asm__("hlt"); }

void Stall(UINTN n) { gBS->Stall(n); }

EFI_STATUS GetMemoryMap(struct MemoryMap* map) {
	if (map->buffer == NULL) {
		return EFI_BUFFER_TOO_SMALL;
	}

	map->map_size = map->buffer_size;
	return gBS->GetMemoryMap(
		&map->map_size,
		(EFI_MEMORY_DESCRIPTOR*)map->buffer,
		&map->map_key,
		&map->descriptor_size,
		&map->descriptor_version);
}

const CHAR16* GetMemoryTypeUnicode(EFI_MEMORY_TYPE type) {
	switch (type) {
	case EfiReservedMemoryType:
		return L"EfiReservedMemoryType";
	case EfiLoaderCode:
		return L"EfiLoaderCode";
	case EfiLoaderData:
		return L"EfiLoaderData";
	case EfiBootServicesCode:
		return L"EfiBootServicesCode";
	case EfiBootServicesData:
		return L"EfiBootServicesData";
	case EfiRuntimeServicesCode:
		return L"EfiRuntimeServicesCode";
	case EfiRuntimeServicesData:
		return L"EfiRuntimeServicesData";
	case EfiConventionalMemory:
		return L"EfiConventionalMemory";
	case EfiUnusableMemory:
		return L"EfiUnusableMemory";
	case EfiACPIReclaimMemory:
		return L"EfiACPIReclaimMemory";
	case EfiACPIMemoryNVS:
		return L"EfiACPIMemoryNVS";
	case EfiMemoryMappedIO:
		return L"EfiMemoryMappedIO";
	case EfiMemoryMappedIOPortSpace:
		return L"EfiMemoryMappedIOPortSpace";
	case EfiPalCode:
		return L"EfiPalCode";
	case EfiPersistentMemory:
		return L"EfiPersistentMemory";
	case EfiMaxMemoryType:
		return L"EfiMaxMemoryType";
	default:
		return L"InvalidMemoryType";
	}
}

EFIAPI EFI_STATUS PrintInfo(UINTN level, const CHAR16* Format, ...) {
	VA_LIST args;
	EFI_STATUS status;
	CHAR16 string[256];

	VA_START(args, Format);
	status = UnicodeVSPrint(string, sizeof(string), Format, args);
	VA_END(args);

	switch (level) {
	case INFO:
		Print(L"[");
		gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTGREEN);
		Print(L"INFO");
		gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE);
		Print(L"] ");
		Print(string);
		break;

	case ERROR:
		Print(L"[");
		gST->ConOut->SetAttribute(gST->ConOut, EFI_LIGHTRED);
		Print(L"ERROR");
		gST->ConOut->SetAttribute(gST->ConOut, EFI_WHITE);
		Print(L"] ");
		Print(string);
		break;
	}
	return status;
}

EFI_STATUS SaveMemoryMap(struct MemoryMap* map, EFI_FILE_PROTOCOL* file) {
	EFI_STATUS status;
	CHAR8 buf[256];
	UINTN len;

	CHAR8* header =
		"Index, Type, Type(name), PhysicalStart, NumberOfPages, Attribute\n";
	len = AsciiStrLen(header);

	status = file->Write(file, &len, header);
	if (EFI_ERROR(status)) {return status;}

	Print(L"map->buffer = %08lx, map->map_size = %08lx\n",
		map->buffer, map->map_size);

	EFI_PHYSICAL_ADDRESS iter;
	int i;
	for (iter = (EFI_PHYSICAL_ADDRESS)map->buffer, i = 0;
		iter < (EFI_PHYSICAL_ADDRESS)map->buffer + map->map_size;
		iter += map->descriptor_size, i++) {
		EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)iter;
		len = AsciiSPrint(
			buf, sizeof(buf),
			"%u, %x, %-ls, %08lx, %lx, %lx\n",
			i, desc->Type, GetMemoryTypeUnicode(desc->Type),
			desc->PhysicalStart, desc->NumberOfPages,
			desc->Attribute & 0xffffflu);
		status = file->Write(file, &len, buf);
		if (EFI_ERROR(status)) {
			return status;
		}
	}

	return EFI_SUCCESS;
}

EFI_STATUS OpenRootDir(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL** root) {
	EFI_STATUS status;
	EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;

	status = gBS->OpenProtocol(
		image_handle,
		&gEfiLoadedImageProtocolGuid,
		(VOID**)&loaded_image,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = gBS->OpenProtocol(
		loaded_image->DeviceHandle,
		&gEfiSimpleFileSystemProtocolGuid,
		(VOID**)&fs,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(status)) {
		return status;
	}

	return fs->OpenVolume(fs, root);
}

EFI_STATUS OpenGOP(EFI_HANDLE image_handle,
	EFI_GRAPHICS_OUTPUT_PROTOCOL** gop) {
	EFI_STATUS status;
	UINTN num_gop_handles = 0;
	EFI_HANDLE* gop_handles = NULL;

	status = gBS->LocateHandleBuffer(
		ByProtocol,
		&gEfiGraphicsOutputProtocolGuid,
		NULL,
		&num_gop_handles,
		&gop_handles);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = gBS->OpenProtocol(
		gop_handles[0],
		&gEfiGraphicsOutputProtocolGuid,
		(VOID**)gop,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(status)) {
		return status;
	}

	gBS->FreePool(gop_handles);

	return EFI_SUCCESS;
}

const CHAR16* GetPixelFormatUnicode(EFI_GRAPHICS_PIXEL_FORMAT fmt) {
	switch (fmt) {
	case PixelRedGreenBlueReserved8BitPerColor:
		return L"PixelRedGreenBlueReserved8BitPerColor";
	case PixelBlueGreenRedReserved8BitPerColor:
		return L"PixelBlueGreenRedReserved8BitPerColor";
	case PixelBitMask:
		return L"PixelBitMask";
	case PixelBltOnly:
		return L"PixelBltOnly";
	case PixelFormatMax:
		return L"PixelFormatMax";
	default:
		return L"InvalidPixelFormat";
	}
}

EFI_STATUS WaitForPressAnyKey() {
	EFI_STATUS status;

	Print(L"Press any key to continue:\n");

	status = gBS->WaitForEvent(1, &(gST->ConIn->WaitForKey), 0);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"WaitForEvent error: %r\n", status);
		Halt();
	}

	EFI_INPUT_KEY key;
	status = gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"ReadKeyStroke error: %r\n", status);
		Halt();
	}
	return EFI_SUCCESS;
}

void CalcLoadAddressRange(Elf64_Ehdr* ehdr, UINT64* first, UINT64* last) {
	Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
	*first = MAX_UINT64;
	*last = 0;
	for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
		if (phdr[i].p_type != PT_LOAD) { continue; }
		*first = MIN(*first, phdr[i].p_vaddr);
		*last = MAX(*last, phdr[i].p_vaddr + phdr[i].p_memsz);
	}
}

void CopyLoadSegments(Elf64_Ehdr* ehdr) {
	Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
	for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
		if (phdr[i].p_type != PT_LOAD) { continue; }

		UINT64 segm_in_file = (UINT64)ehdr + phdr[i].p_offset;
		CopyMem((VOID*)phdr[i].p_vaddr, (VOID*)segm_in_file, phdr[i].p_filesz);

		UINTN remain_bytes = phdr[i].p_memsz - phdr[i].p_filesz;
		SetMem((VOID*)(phdr[i].p_vaddr + phdr[i].p_filesz), remain_bytes, 0);
	}
}

EFI_STATUS ReadFile(EFI_FILE_PROTOCOL* file, VOID** buffer) {
	EFI_STATUS status;

	UINTN file_info_size = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 12;
	UINT8 file_info_buffer[file_info_size];
	status = file->GetInfo(
		file, &gEfiFileInfoGuid,
		&file_info_size, file_info_buffer);
	if (EFI_ERROR(status)) {
		return status;
	}

	EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
	UINTN file_size = file_info->FileSize;

	status = gBS->AllocatePool(EfiLoaderData, file_size, buffer);
	if (EFI_ERROR(status)) { return status; }

	return file->Read(file, &file_size, *buffer);
}

EFI_STATUS OpenBlockIoProtocolForLoadedImage(
	EFI_HANDLE image_handle, EFI_BLOCK_IO_PROTOCOL** block_io) {
	EFI_STATUS status;
	EFI_LOADED_IMAGE_PROTOCOL* loaded_image;

	status = gBS->OpenProtocol(
		image_handle,
		&gEfiLoadedImageProtocolGuid,
		(VOID**)&loaded_image,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(status)) { return status; }

	status = gBS->OpenProtocol(
		loaded_image->DeviceHandle,
		&gEfiBlockIoProtocolGuid,
		(VOID**)block_io,
		image_handle, // agent handle
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

	return status;
}

EFI_STATUS ReadBlocks(
	EFI_BLOCK_IO_PROTOCOL* block_io, UINT32 media_id,
	UINTN read_bytes, VOID** buffer) {
	EFI_STATUS status;

	status = gBS->AllocatePool(EfiLoaderData, read_bytes, buffer);
	if (EFI_ERROR(status)) { return status; }

	status = block_io->ReadBlocks(
		block_io,
		media_id,
		0, // start LBA
		read_bytes,
		*buffer);

	return status;
}

EFI_STATUS EFIAPI UefiMain(
	EFI_HANDLE image_handle,
	EFI_SYSTEM_TABLE* system_table) {
	EFI_STATUS status;

	Print(L"Hello, World!\nThis is laplus OS!");

	CHAR8 memmap_buf[4096 * 4];
	struct MemoryMap memmap = { sizeof(memmap_buf), memmap_buf, 0, 0, 0, 0 };
	status = GetMemoryMap(&memmap);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"Failed to get memory map: %r\n", status);
		Halt();
	}

	//解像度をSXGAに
	int vga_mode = 0;
	for (int i = 0; i < gop->Mode->MaxMode; ++i) {
		UINTN gop_info_size;
		EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* gop_info;
		gop->QueryMode(gop, i, &gop_info_size, &gop_info);
		if (gop_info->HorizontalResolution == RES_HORZ &&
			gop_info->VerticalResolution == RES_VERT) {
			vga_mode = i;
			break;
		}
	}

	//変更できるかは機種によるのでできない場合はその旨表示
	status = gop->SetMode(gop, vga_mode);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"Failed to change resolution: %r\n", status);
		Halt();
	}

	Print(L"Booting laplus OS.");
	for (int i = 0; i < 5; ++i) {
		Stall(1000000);
		Print(L".");
	}
	Print(L"\n");

	EFI_FILE_PROTOCOL* root_dir;
	status = OpenRootDir(image_handle, &root_dir);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"Failed to open root directory: %r\n", status);
		Halt();
	}

	EFI_FILE_PROTOCOL* memmap_file;
	status = root_dir->Open(
		root_dir, &memmap_file, L"\\memmap",
		EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"Failed to open file '\\memmap': %r\n", status);
		PrintInfo(INFO, L"Ignored.\n");
	}
	else {
		status = SaveMemoryMap(&memmap, memmap_file);
		if (EFI_ERROR(status)) {
			PrintInfo(ERROR, L"Failed to save memory map: %r\n", status);
			Halt();
		}
		status = memmap_file->Close(memmap_file);
		if (EFI_ERROR(status)) {
			PrintInfo(ERROR, L"Failed to close memory map: %r\n", status);
			Halt();
		}
	}

	EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
	status = OpenGOP(image_handle, &gop);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"Failed to open GOP: %r\n", status);
		Halt();
	}

	Print(L"Resolution: %ux%u, Pixel Format: %s, %u pixels/line\n",
		gop->Mode->Info->HorizontalResolution,
		gop->Mode->Info->VerticalResolution,
		GetPixelFormatUnicode(gop->Mode->Info->PixelFormat),
		gop->Mode->Info->PixelsPerScanLine);
	Print(L"Frame Buffer: 0x%0lx - 0x%0lx, Size: %lu bytes\n",
		gop->Mode->FrameBufferBase,
		gop->Mode->FrameBufferBase + gop->Mode->FrameBufferSize,
		gop->Mode->FrameBufferSize);
	Stall(3000000);

	UINT8* frame_buffer = (UINT8*)gop->Mode->FrameBufferBase;
	for (UINTN i = 0; i < gop->Mode->FrameBufferSize; ++i) { frame_buffer[i] = 255; }

	EFI_FILE_PROTOCOL* kernel_file;
	status = root_dir->Open(
		root_dir, &kernel_file, L"\\kernel.elf",
		EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"Failed to open file '\\kernel.elf': %r\n", status);
		Halt();
	}

	VOID* kernel_buffer;
	status = ReadFile(kernel_file, &kernel_buffer);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"Error: %r\n", status);
		Halt();
	}

	Elf64_Ehdr* kernel_ehdr = (Elf64_Ehdr*)kernel_buffer;
	UINT64 kernel_first_addr, kernel_last_addr;
	CalcLoadAddressRange(kernel_ehdr, &kernel_first_addr, &kernel_last_addr);

	UINTN num_pages = (kernel_last_addr - kernel_first_addr + 0xfff) / 0x1000;
	status = gBS->AllocatePages(AllocateAddress, EfiLoaderData,
		num_pages, &kernel_first_addr);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"Failed to allocate pages: %r\n", status);
		Halt();
	}

	CopyLoadSegments(kernel_ehdr);
	Print(L"Kernel: 0x%0lx - 0x%0lx\n", kernel_first_addr, kernel_last_addr);

	status = gBS->FreePool(kernel_buffer);
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"Failed to free pool: %r\n", status);
		Halt();
	}

	VOID* volume_image;

	EFI_FILE_PROTOCOL* volume_file;
	status = root_dir->Open(
		root_dir, &volume_file, L"\\fat_disk",
		EFI_FILE_MODE_READ, 0);
	if (status == EFI_SUCCESS) {
		status = ReadFile(volume_file, &volume_image);
		if (EFI_ERROR(status)) {
			PrintInfo(ERROR, L"Failed to read volume file: %r\n", status);
			Halt();
		}
	}
	else {
		EFI_BLOCK_IO_PROTOCOL* block_io;
		status = OpenBlockIoProtocolForLoadedImage(image_handle, &block_io);
		if (EFI_ERROR(status)) {
			PrintInfo(ERROR, L"Failed to open Block I/O Protocol: %r\n", status);
			Halt();
		}

		EFI_BLOCK_IO_MEDIA* media = block_io->Media;
		UINTN volume_bytes = (UINTN)media->BlockSize * (media->LastBlock + 1);
		if (volume_bytes > 32 * 1024 * 1024) {
			volume_bytes = 32 * 1024 * 1024;
		}

		Print(L"Reading %lu bytes (Present %d, BlockSize %u, LastBlock %u)\n",
			volume_bytes, media->MediaPresent, media->BlockSize, media->LastBlock);

		status = ReadBlocks(block_io, media->MediaId, volume_bytes, &volume_image);
		if (EFI_ERROR(status)) {
			PrintInfo(ERROR, L"Failed to read blocks: %r\n", status);
			Halt();
		}
	}

	struct FrameBufferConfig config = {
	  (UINT8*)gop->Mode->FrameBufferBase,
	  gop->Mode->Info->PixelsPerScanLine,
	  gop->Mode->Info->HorizontalResolution,
	  gop->Mode->Info->VerticalResolution,
	  0
	};
	switch (gop->Mode->Info->PixelFormat) {
	case PixelRedGreenBlueReserved8BitPerColor:
		config.pixel_format = kPixelRGBResv8BitPerColor;
		break;
	case PixelBlueGreenRedReserved8BitPerColor:
		config.pixel_format = kPixelBGRResv8BitPerColor;
		break;
	default:
		Print(L"Unimplemented pixel format: %d\n", gop->Mode->Info->PixelFormat);
		Halt();
	}

	status = WaitForPressAnyKey();
	if (EFI_ERROR(status)) {
		PrintInfo(ERROR, L"WaitForPressAnyKey error: %r\n", status);
		Halt();
	}

	status = gBS->ExitBootServices(image_handle, memmap.map_key);
	if (EFI_ERROR(status)) {
		status = GetMemoryMap(&memmap);
		if (EFI_ERROR(status)) {
			PrintInfo(ERROR, L"Failed to get memory map: %r\n", status);
			Halt();
		}
		status = gBS->ExitBootServices(image_handle, memmap.map_key);
		if (EFI_ERROR(status)) {
			PrintInfo(ERROR, L"Could not exit boot service: %r\n", status);
			Halt();
		}
	}

	UINT64 entry_addr = *(UINT64*)(kernel_first_addr + 24);

	VOID* acpi_table = NULL;
	for (UINTN i = 0; i < system_table->NumberOfTableEntries; ++i) {
		if (CompareGuid(&gEfiAcpiTableGuid,
			&system_table->ConfigurationTable[i].VendorGuid)) {
			acpi_table = system_table->ConfigurationTable[i].VendorTable;
			break;
		}
	}

	typedef void EntryPointType(const struct FrameBufferConfig*,
		const struct MemoryMap*,
		const VOID*,
		VOID*,
		EFI_RUNTIME_SERVICES*);
	EntryPointType* entry_point = (EntryPointType*)entry_addr;
	entry_point(&config, &memmap, acpi_table, volume_image, gRT);

	Print(L"All done!\n");
	Stall(1000000);

	while (1);
	return EFI_SUCCESS;
}