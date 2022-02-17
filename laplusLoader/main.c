#include  <Uefi.h>
#include  <Library/UefiLib.h>
#include  "frame_buffer_config.hpp"
#include  <Library/UefiLib.h>
#include  <Library/UefiBootServicesTableLib.h>
#include  <Library/PrintLib.h>
#include  <Library/MemoryAllocationLib.h>
#include  <Protocol/LoadedImage.h>
#include  <Protocol/SimpleFileSystem.h>
#include  <Protocol/DiskIo2.h>
#include  <Protocol/BlockIo.h>
#include  <Guid/FileInfo.h>
#include"frame_buffer_config.hpp"
#include"elf.hpp"

void Halt(void) { while (1) __asm__("hlt"); }

//メモリーマップのリスト
struct MemoryMap {
	UINTN buffer_size;
	VOID* buffer;
	UINTN map_size;
	UINTN map_key;
	UINTN descriptor_size;
	UINT32 descriptor_version;
};

//メモリーマップの取得
EFI_STATUS GetMemoryMap(struct MemoryMap* map) {
	if (map->buffer == NULL) { return EFI_BUFFER_TOO_SMALL; }

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
	case EfiReservedMemoryType: return L"EfiReservedMemoryType";
	case EfiLoaderCode: return L"EfiLoaderCode";
	case EfiLoaderData: return L"EfiLoaderData";
	case EfiBootServicesCode: return L"EfiBootServicesCode";
	case EfiBootServicesData: return L"EfiBootServicesData";
	case EfiRuntimeServicesCode: return L"EfiRuntimeServicesCode";
	case EfiRuntimeServicesData: return L"EfiRuntimeServicesData";
	case EfiConventionalMemory: return L"EfiConventionalMemory";
	case EfiUnusableMemory: return L"EfiUnusableMemory";
	case EfiACPIReclaimMemory: return L"EfiACPIReclaimMemory";
	case EfiACPIMemoryNVS: return L"EfiACPIMemoryNVS";
	case EfiMemoryMappedIO: return L"EfiMemoryMappedIO";
	case EfiMemoryMappedIOPortSpace: return L"EfiMemoryMappedIOPortSpace";
	case EfiPalCode: return L"EfiPalCode";
	case EfiPersistentMemory: return L"EfiPersistentMemory";
	case EfiMaxMemoryType: return L"EfiMaxMemoryType";
	default: return L"InvalidMemoryType";
	}
}

const CHAR16* GetPixelFormatUnicode(EFI_PIXEL_GRAPHICS_FORMAT fmt) {
	switch (fmt) {
	case PixelRedGreenBlueReserved8bitPerColor: return L"PixelRedGreenBlueReserved 8bitPerColor";
	case PixelBlueGreenRedReserved8bitPerColor: return L"PixelBlueGreenRedReservedPerColor";
	case PixelBitMask: return L"PixelBitMask";
	case PixelBitOnly:return L"PixelBitOnly";
	case PixelFormatMax:return L"PixelFormatMax";
	default: return L"Invalid PixelFormat";
	}
}

EFI_STATUS SaveMemoryMap(struct MemoryMap* map, EFI_FILE_PROTOCOL* file) {
	EFI_STATUS status;
	CHAR8 buf[256];
	UINTN len;

	CHAR8* header = "Index, Type, Type(name), PhysicalStart, NumberOfPages, Attribute\n";
	len = AsciiStrLen(header);
	status = file->Write(file, &len, header);
	if (EFI_ERROR(status)) { return status; }

	Print(L"map->buffer = %08lx, map->map_size = %08lx\n", map->buffer, map->map_size);

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
		if (EFI_ERROR(status)) { return status; }
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
	if (EFI_ERROR(status)) { return status; }

	status = gBS->OpenProtocol(
		loaded_image->DeviceHandle,
		&gEfiSimpleFileSystemProtocolGuid,
		(VOID**)&fs,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(status)) { return status; }

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
	if (EFI_ERROR(status)) { return status; }

	status = gBS->OpenProtocol(
		gop_handles[0],
		&gEfiGraphicsOutputProtocolGuid,
		(VOID**)gop,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(status)) { return status; }

	FreePool(gop_handles);

	return EFI_SUCCESS;
}

void CalcLoadAddressRange(Elf64_Ehdr* ehdr, UINT64* first, UINT64* last) {
	Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
	*first = MAX_UINT64;
	*last = 0;
	for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
		if (phdr[i].p_type != PT_LOAD) continue;
		*first = MIN(*first, phdr[i].p_vaddr);
		*last = MAX(*last, phdr[i].p_vaddr + phdr[i].p_memsz);
	}
}

void CopyLoadSegments(Elf64_Ehdr* ehdr) {
	Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
	for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
		if (phdr[i].p_type != PT_LOAD) continue;

		UINT64 segm_in_file = (UINT64)ehdr + phdr[i].p_offset;
		CopyMem((VOID*)phdr[i].p_vaddr, (VOID*)segm_in_file, phdr[i].p_filesz);

		UINTN remain_bytes = phdr[i].p_memsz - phdr[i].p_filesz;
		SetMem((VOID*)(phdr[i].p_vaddr + phdr[i].p_filesz), remain_bytes, 0);
	}
}

EFI_STATUS EFIAPI UefiMain(
	EFI_HANDLE image_handle,
	EFI_SYSTEM_TABLE* system_table) {
	//Print(L"Hello,World!\n This is laplus OS!");

	//GOP(Graphics Output Protocol)を使って画面を白に塗りつぶし
	EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
	OpenGOP(image_handle, &gop);
	Print(L"Resolution: %ux%u, Pixel Format: %s, %u pixels/line\n",
		gop->Mode->Info->HorizontalResolution,
		gop->Mode->Info->VerticalResolution,
		GetPixelFormatUnicode(gop->Mode->Info->PixelFormat),
		gop->Mode->Info->PixelsPerScanLine);
	Print(L"Frame Buffer: 0x%0lx - 0x%0lx, Size: %lu bytes\n",
		gop->Mode->FrameBufferBase,
		gop->Mode->FrameBufferBase + gop->Mode->FrameBufferSize,
		gop->Mode->FrameBufferSize);

	UINT8* frame_buffer = (UINT8*)gop->Mode->FrameBufferBase;
	for (UINTN i = 0; i < gop->Mode->FrameBufferSize; ++i) { frame_buffer[i] = 255; }
	//GOP塗りつぶし終了

	//カーネルファイルを読み込む部分
	EFI_FILE_PROTOCOL* kernel_file;
	root_dir->Open(
		root_dir, &kernel_file, L"\\kernel.elf",//read elf file
		EFI_FILE_MODE_READ, 0);

	UINTN file_info_size = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 12;
	UINT8 file_info_buffer[file_info_size];
	kernel_file->GetInfo(
		kernel_file, &gEfiFileInfoGuid,
		&file_info_size, file_info_buffer);

	EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
	UINTN kernel_file_size = file_info->FileSize;

	EFI_PHYSICAL_ADDRESS kernel_base_addr = 0x100000;
	gBS->AllocatePages(
		AllocateAddress, EfiLoaderData,
		(kernel_file_size + 0xfff) / 0x1000, &kernel_base_addr);
	kernel_file->Read(kernel_file, &kernel_file_size, (VOID*)kernel_base_addr);
	Print(L"Kernel: 0x%0lx (%lu bytes)\n", kernel_base_addr, kernel_file_size);
	//カーネル読み込み終了


	//read kernel
	EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
	UININ kernel_info_size = file.info->FileSize;

	VOID* kernel_buffer;
	status = gBS->AllocatePool(EfiLoaderData, kernel_file_size, &kernel_buffer);

	if (EFI_ERROR(status)) {
		Print(L"Failed to read allocate pool! Status: %r\n", status);
		Halt();
	}

	status = kernel_file->Read(kernel_file, &kernel_file, size, kernel_buffer);
	if (EFI_ERROR(status)) {
		Print(L"Error! Status: %r\n", status);
		Halt();
	}
	//read終了

	//コピー先のメモリ領域確保
	Elf64_Ehdr* kernel_ehdr = (Elf64_Ehdr*)kernel_buffer;
	UINT64 kernel_first_addr, kernel_last_addr;
	CalcLoadAddressRange(kernel_endr, &kernel_first_addr, &kernel_last_addr);

	UINTN num_pages = (kernel_last_addr - kernel_first_addr + 0xfff) / 0x1000;
	status = gBS->AllocatePool(AllocateAddress, EfiLoaderData, num_pages, &kernel_first_buffer);
	if (EFI_ERROR(status)) {
		Print(L"Failed to read allocate pool! Status: %r\n", status);
		Halt();
	}
	//確保終了

	//カーネル起動前にUEFI BIOSのブートサービスの停止
	EFI_STATUS status;
	status = gBS->ExitBootServices(image_handle, memmap.map_key);
	if (EFI_ERROR(status)) {
		status = GetMemoryMap(&memmap);
		if (EFI_ERROR(status)) {
			Print(L"failed to get memory map: %r\n", status);
			while (1);
		}
		status = gBS->ExitBootServices(image_handle, memmap.map_key);
		if (EFI_ERROR(status)) {
			Print(L"Could not exit boot service: %r\n", status);
			while (1);
		}
	}
	//停止終了

	//カーネル起動処理
	UINT64 entry_addr = *(UINT64*)(kernel_base_addr + 24);
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

	typedef void EntryPointType(const struct FrameBufferConfig*);
	EntryPointType* entry_point = (EntryPointType*)entry_addr;
	entry_point(&config);
	//カーネル起動処理終了

	Print(L"All done!\n");

	while (1);
	return EFI_SUCCESS;
}