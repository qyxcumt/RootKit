#include "define.h"
#pragma once

#ifdef LOG_OFF
#define DBG_TRACE(src,msg)
#define DBG_PRINT1(arg1)
#define DBG_PRINT2(fmt,arg1)
#define DBG_PRINT3(fmt,arg1,arg2)
#else
#define DBG_TRACE(src,msg) DbgPrint("[%s]: %s\n",src,msg)
#define DBG_PRINT1(arg1) DbgPrint("%s",arg1)
#define DBG_PRINT2(fmt,arg1) DbgPrint(fmt,arg1)
#define DBG_PRINT3(fmt,arg1,arg2,arg3) DbgPring(fmt,arg1,arg2)
#endif
