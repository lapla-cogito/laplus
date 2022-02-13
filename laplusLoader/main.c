#include  <Uefi.h>
#include  <Library/UefiLib.h>

EFI_STATUS EFIAPI UefiMain(
	EFI_HANDLE image_handle,
	EFI_SYSTEM_TABLE* system_table) {
	Print(L"Hello,World!\n This is laplus OS!");
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