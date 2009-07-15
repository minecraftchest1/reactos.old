/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        tcpip/iinfo.c
 * PURPOSE:     Per-interface information.
 * PROGRAMMERS: Art Yerkes
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"

TDI_STATUS InfoTdiQueryGetInterfaceMIB(TDIEntityID *ID,
				       PIP_INTERFACE Interface,
				       PNDIS_BUFFER Buffer,
				       PUINT BufferSize) {
    TDI_STATUS Status = TDI_INVALID_REQUEST;
    PIFENTRY OutData;
    PLAN_ADAPTER IF = (PLAN_ADAPTER)Interface->Context;
    PCHAR IFDescr;
    ULONG Size;
    UINT DescrLenMax = MAX_IFDESCR_LEN - 1;
    NDIS_STATUS NdisStatus;

    TI_DbgPrint(DEBUG_INFO,
		("Getting IFEntry MIB (IF %08x LA %08x) (%04x:%d)\n",
		 Interface, IF, ID->tei_entity, ID->tei_instance));

    OutData =
	(PIFENTRY)exAllocatePool( NonPagedPool,
				  sizeof(IFENTRY) + MAX_IFDESCR_LEN );

    if( !OutData ) return TDI_NO_RESOURCES; /* Out of memory */

    RtlZeroMemory( OutData, sizeof(IFENTRY) + MAX_IFDESCR_LEN );

    OutData->Index = Interface->Index;
    /* viz: tcpip keeps those indices */
    OutData->Type = Interface ==
        Loopback ? MIB_IF_TYPE_LOOPBACK : MIB_IF_TYPE_ETHERNET;
    OutData->Mtu = Interface->MTU;
    TI_DbgPrint(DEBUG_INFO,
		("Getting interface speed\n"));
    OutData->PhysAddrLen = Interface->AddressLength;
    OutData->AdminStatus = MIB_IF_ADMIN_STATUS_UP;
    /* NDIS_HARDWARE_STATUS -> ROUTER_CONNECTION_STATE */
    Status = GetInterfaceConnectionStatus( Interface, &OutData->OperStatus );

    /* Not sure what to do here, but not ready seems a safe bet on failure */
    if( !NT_SUCCESS(Status) )
        OutData->OperStatus = NdisHardwareStatusNotReady;

    IFDescr = (PCHAR)&OutData[1];

    if( IF ) {
	GetInterfaceSpeed( Interface, (PUINT)&OutData->Speed );
	TI_DbgPrint(DEBUG_INFO,
		    ("IF Speed = %d * 100bps\n", OutData->Speed));
	memcpy(OutData->PhysAddr,Interface->Address,Interface->AddressLength);
	TI_DbgPrint(DEBUG_INFO, ("Got HWAddr\n"));

        memcpy(&OutData->InOctets, &Interface->Stats, sizeof(SEND_RECV_STATS));

        NdisStatus = NDISCall(IF,
                              NdisRequestQueryInformation,
                              OID_GEN_XMIT_ERROR,
                              &OutData->OutErrors,
                              sizeof(ULONG));
        if (NdisStatus != NDIS_STATUS_SUCCESS)
            OutData->OutErrors = 0;

        TI_DbgPrint(DEBUG_INFO, ("OutErrors = %d\n", OutData->OutErrors));

        NdisStatus = NDISCall(IF,
                              NdisRequestQueryInformation,
                              OID_GEN_RCV_ERROR,
                              &OutData->InErrors,
                              sizeof(ULONG));
        if (NdisStatus != NDIS_STATUS_SUCCESS)
            OutData->InErrors = 0;

        TI_DbgPrint(DEBUG_INFO, ("InErrors = %d\n", OutData->InErrors));
    }

    GetInterfaceName( Interface, IFDescr, MAX_IFDESCR_LEN - 1 );
    DescrLenMax = strlen( IFDescr ) + 1;

    TI_DbgPrint(DEBUG_INFO, ("Copied in name %s\n", IFDescr));
    OutData->DescrLen = DescrLenMax;
    IFDescr += DescrLenMax;
    Size = IFDescr - (PCHAR)OutData + 1;

    TI_DbgPrint(DEBUG_INFO, ("Finished IFEntry MIB (%04x:%d) size %d\n",
			    ID->tei_entity, ID->tei_instance, Size));

    Status = InfoCopyOut( (PCHAR)OutData, Size, Buffer, BufferSize );
    exFreePool( OutData );

    TI_DbgPrint(DEBUG_INFO,("Returning %x\n", Status));

    return Status;
}

TDI_STATUS InfoTdiQueryGetArptableMIB(TDIEntityID *ID,
				      PIP_INTERFACE Interface,
				      PNDIS_BUFFER Buffer,
				      PUINT BufferSize) {
    NTSTATUS Status;
    ULONG NumNeighbors = NBCopyNeighbors( Interface, NULL );
    ULONG MemSize = NumNeighbors * sizeof(IPARP_ENTRY);
    PIPARP_ENTRY ArpEntries =
	exAllocatePoolWithTag
	( NonPagedPool, MemSize, FOURCC('A','R','P','t') );

    if( !ArpEntries ) return STATUS_NO_MEMORY;
    NBCopyNeighbors( Interface, ArpEntries );

    Status = InfoCopyOut( (PVOID)ArpEntries, MemSize, Buffer, BufferSize );

    exFreePool( ArpEntries );

    return Status;
}

TDI_STATUS InfoTdiQueryGetArpCapability(TDIEntityID *ID,
					PIP_INTERFACE Interface,
					PNDIS_BUFFER Buffer,
					PUINT BufferSize) {
    NTSTATUS Status;
    ULONG Capability = 0x280;

    TI_DbgPrint(MID_TRACE,("Copying out %d bytes (AT_ENTITY capability)\n",
			   sizeof(Capability)));
    Status = InfoCopyOut
	( (PVOID)&Capability, sizeof(Capability), Buffer, BufferSize );

    return Status;
}

TDI_STATUS InfoInterfaceTdiQueryEx( UINT InfoClass,
				    UINT InfoType,
				    UINT InfoId,
				    PVOID Context,
				    TDIEntityID *id,
				    PNDIS_BUFFER Buffer,
				    PUINT BufferSize ) {
    if( InfoClass == INFO_CLASS_GENERIC &&
	InfoType == INFO_TYPE_PROVIDER &&
	InfoId == ENTITY_TYPE_ID &&
	id->tei_entity == AT_ENTITY ) {
	return InfoTdiQueryGetArpCapability( id, Context, Buffer, BufferSize );
    } else if( InfoClass == INFO_CLASS_PROTOCOL &&
	       InfoType == INFO_TYPE_PROVIDER &&
	       InfoId == IF_MIB_STATS_ID ) {
	return InfoTdiQueryGetInterfaceMIB( id, Context, Buffer, BufferSize );
    } else if( InfoClass == INFO_CLASS_GENERIC &&
	       InfoType == INFO_TYPE_PROVIDER &&
	       InfoId == ENTITY_TYPE_ID ) {
	ULONG Temp = IF_MIB;
	return InfoCopyOut( (PCHAR)&Temp, sizeof(Temp), Buffer, BufferSize );
    } else if( InfoClass == INFO_CLASS_PROTOCOL &&
	       InfoType == INFO_TYPE_PROVIDER &&
	       InfoId == IP_MIB_ARPTABLE_ENTRY_ID ) {
	return InfoTdiQueryGetArptableMIB( id, Context, Buffer, BufferSize );
    } else
	return TDI_INVALID_REQUEST;
}

TDI_STATUS InfoInterfaceTdiSetEx( UINT InfoClass,
				  UINT InfoType,
				  UINT InfoId,
				  PVOID Context,
				  TDIEntityID *id,
				  PCHAR Buffer,
				  UINT BufferSize ) {
    TI_DbgPrint(DEBUG_INFO, ("Got Request: Class %x Type %x Id %x, EntityID %x:%x\n",
                InfoClass, InfoId, id->tei_entity, id->tei_instance));
    return TDI_INVALID_REQUEST;
}
