#pragma once
#include <Uefi.h>

struct MemoryMap {
	UINTN buffer_size;
	VOID* buffer;
	UINTN map_size;
	UINTN map_key;
	UINTN descriptor_size;
	UINT32 descriptor_version;
};