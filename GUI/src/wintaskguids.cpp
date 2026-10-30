// Defines the Task Scheduler COM GUIDs (CLSID_TaskScheduler, IID_ITaskService,
// IID_IExecAction, ...) that espops/wintasks.cpp references. MinGW-w64 ships
// them in libtaskschd.a for i686/x86_64 only -- there is no ARM64 import
// library -- so instead of linking -ltaskschd this translation unit includes
// <initguid.h> first, which turns the DEFINE_GUID declarations in
// <taskschd.h> into definitions. Kept outside espops/ (parity-locked with
// SteamDeck_rEFInd) and compiled into the espops target on Windows only.
// Linking order makes this safe even where libtaskschd.a exists: the linker
// takes these definitions and never pulls the archive member.
#include <initguid.h>
#include <taskschd.h>
