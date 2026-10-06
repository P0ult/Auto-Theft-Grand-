// Prints a stack trace when a native test crashes (Windows: DbgHelp). Build with DEBUG=1 Tools/native.sh
// so the symbols are there. Include once and call InstallCrashTrace() at the start of main.
#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI CrashTraceFilter(EXCEPTION_POINTERS* ep) {
	HANDLE proc = GetCurrentProcess(), thread = GetCurrentThread();
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	SymInitialize(proc, nullptr, TRUE);
	CONTEXT ctx = *ep->ContextRecord;
	STACKFRAME64 f = {};
	f.AddrPC.Offset = ctx.Rip; f.AddrPC.Mode = AddrModeFlat;
	f.AddrFrame.Offset = ctx.Rbp; f.AddrFrame.Mode = AddrModeFlat;
	f.AddrStack.Offset = ctx.Rsp; f.AddrStack.Mode = AddrModeFlat;
	fprintf(stderr, "crash: exception 0x%08lx at %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
	for (int i = 0; i < 40; i++) {
		if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &f, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
		char buf[sizeof(SYMBOL_INFO) + 512] = {};
		SYMBOL_INFO* sym = (SYMBOL_INFO*)buf; sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 511;
		DWORD64 d64 = 0; DWORD d32 = 0;
		IMAGEHLP_LINE64 line = {}; line.SizeOfStruct = sizeof line;
		const char* name = SymFromAddr(proc, f.AddrPC.Offset, &d64, sym) ? sym->Name : "?";
		if (SymGetLineFromAddr64(proc, f.AddrPC.Offset, &d32, &line)) fprintf(stderr, "  %s  %s:%lu\n", name, line.FileName, line.LineNumber);
		else fprintf(stderr, "  %s\n", name);
	}
	fflush(stderr);
	return EXCEPTION_EXECUTE_HANDLER;
}
static void InstallCrashTrace() { SetUnhandledExceptionFilter(CrashTraceFilter); }
#else
static void InstallCrashTrace() {}
#endif
