// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * Adapted from edk2 source code (ArmCacheMaintenanceLib, ArmLib):
 *
 * Copyright (c) 2008 - 2010, Apple Inc. All rights reserved.
 * Copyright (c) 2011 - 2021, ARM Limited. All rights reserved.
 */

#include <efi.h>

#include "CacheMaintenanceLib.h"

/* These 3 functions are implemented in assembly files in edk2 */

static void ArmDataSynchronizationBarrier(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
}

static UINTN ArmCacheInfo(void)
{
	UINTN val;
	__asm__("mrs %0, CTR_EL0" : "=r"(val));
	return val;
}

static void ArmCleanInvalidateDataCacheEntryByMVA(UINTN va)
{
	__asm__ volatile("dc civac, %0" :: "r"(va) : "memory");
}

static
UINTN
EFIAPI
ArmDataCacheLineLength (
  VOID
  )
{
  return 4 << ((ArmCacheInfo () >> 16) & 0xf); // CTR_EL0.DminLine
}

typedef VOID (*LINE_OPERATION)(
  UINTN
  );

STATIC
VOID
CacheRangeOperation (
  IN  VOID            *Start,
  IN  UINTN           Length,
  IN  LINE_OPERATION  LineOperation,
  IN  UINTN           LineLength
  )
{
  UINTN  ArmCacheLineAlignmentMask;
  // Align address (rounding down)
  UINTN  AlignedAddress;
  UINTN  EndAddress;

  ArmCacheLineAlignmentMask = LineLength - 1;
  AlignedAddress            = (UINTN)Start - ((UINTN)Start & ArmCacheLineAlignmentMask);
  EndAddress                = (UINTN)Start + Length;

  // Perform the line operation on an address in each cache line
  while (AlignedAddress < EndAddress) {
    LineOperation (AlignedAddress);
    AlignedAddress += LineLength;
  }

  ArmDataSynchronizationBarrier ();
}

VOID *
EFIAPI
WriteBackInvalidateDataCacheRange (
  IN      VOID   *Address,
  IN      UINTN  Length
  )
{
  CacheRangeOperation (
    Address,
    Length,
    ArmCleanInvalidateDataCacheEntryByMVA,
    ArmDataCacheLineLength ()
    );
  return Address;
}
