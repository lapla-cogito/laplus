#include  <Uefi.h>
#include  <Library/UefiLib.h>

EFI_STATUS EFIAPI UefiMain(
	EFI_HANDLE image_handle,
	EFI_SYSTEM_TABLE* system_table) {
	Print(L"Hello,World!\n This is laplus OS!");

	//カーネルファイルを読み込む部分
	EFI_FILE_PROTOCOL* kernel_file;
	root_dir->Open(
		root_dir, &kernel_file, L"\\kernel.elf",
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

	//カーネル起動
	UINT64 entry_addr = *(UINT64*)(kernel_base_addr + 24);
	typedef void EntryPointType(void);
	EntryPointType* entry_point = (EntryPointType*)entry_addr;
	entry_point();
	//起動終了

	while (1);
	return EFI_SUCCESS;
}

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