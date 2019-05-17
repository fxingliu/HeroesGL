/*
	MIT License

	Copyright (c) 2019 Oleksiy Ryabchun

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/

#pragma once
#define WIN32_LEAN_AND_MEAN
//#define WINVER 0x0400

#include "windows.h"
//#include "stdio.h"
#include "math.h"
#include "ddraw.h"

#define WC_DRAW "drawclass"

#define RES_WIDTH 640
#define RES_HEIGHT 480
#define SHADOW_OFFSET 6

typedef HRESULT(__stdcall* DIRECTDRAWCREATE)(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter);

extern DIRECTDRAWCREATE DDCreate;

typedef HANDLE(__stdcall* CREATEACTCTXA)(ACTCTX* pActCtx);
typedef VOID(__stdcall* RELEASEACTCTX)(HANDLE hActCtx);
typedef BOOL(__stdcall* ACTIVATEACTCTX)(HANDLE hActCtx, ULONG_PTR* lpCookie);
typedef BOOL(__stdcall* DEACTIVATEACTCTX)(DWORD dwFlags, ULONG_PTR ulCookie);

extern CREATEACTCTXA CreateActCtxC;
extern RELEASEACTCTX ReleaseActCtxC;
extern ACTIVATEACTCTX ActivateActCtxC;
extern DEACTIVATEACTCTX DeactivateActCtxC;

#ifndef _FILE_DEFINED
#define _FILE_DEFINED
typedef struct _iobuf {
	void* _Placeholder;
} FILE;
#endif

extern "C"
{
	_CRTIMP int __cdecl sprintf(char*, const char*, ...);
	_CRTIMP FILE* __cdecl fopen(const char*, const char*);
	_CRTIMP int __cdecl fclose(FILE*);
}

#define MemoryAlloc malloc
#define MemoryFree free
#define MemorySet memset
#define MemoryZero(dst, size) memset(dst, 0, size)
#define MemoryCopy memcpy
#define MathCeil ceil
#define MathFloor floor
#define StrPrint sprintf
#define StrCompare strcmp
#define StrCompareInsensitive _stricmp
#define StrCopy strcpy
#define StrCat strcat
#define StrChar strchr
#define StrLastChar strrchr
#define StrStr strstr
#define StrToAnsi wcstombs
#define StrDuplicate _strdup
#define StrToInt atoi
#define FileOpen fopen
#define FileClose fclose
#define Random rand
#define SeedRandom srand
#define Exit exit

DOUBLE __fastcall MathRound(DOUBLE);

extern HMODULE hDllModule;
extern HANDLE hActCtx;

VOID LoadKernel32();
VOID LoadWinG32();
VOID LoadDDraw();