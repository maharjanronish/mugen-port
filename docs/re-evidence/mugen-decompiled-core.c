/* ============================================================
   entry point (0x4dc80a) - PE header entry
   Decompiled by REA 6.7.0 (Ghidra 12.1.4 backend)
   ============================================================ */


void entry(void)

{
  ___security_init_cookie();
  ___tmainCRTStartup();
  return;
}



/* ============================================================
   ___tmainCRTStartup (0x4dc54a) - MSVC 2008 CRT startup
   Decompiled by REA 6.7.0 (Ghidra 12.1.4 backend)
   ============================================================ */


/* WARNING: Function: __SEH_prolog4 replaced with injection: SEH_prolog4 */
/* WARNING: Function: __SEH_epilog4 replaced with injection: EH_epilog3 */
/* Library Function - Single Match
    ___tmainCRTStartup
   
   Library: Visual Studio 2008 Release */

int ___tmainCRTStartup(void)

{
  byte bVar1;
  void *Exchange;
  void *pvVar2;
  BOOL BVar3;
  int iVar4;
  bool bVar5;
  byte *pbVar6;
  _STARTUPINFOA local_6c;
  byte *local_24;
  uint local_20;
  undefined4 uStack_c;
  undefined4 local_8;
  
  uStack_c = 0x4dc556;
  bVar5 = false;
  local_20 = 0;
  local_8 = 0;
  GetStartupInfoA(&local_6c);
  Exchange = StackBase;
  local_8 = 1;
  do {
    pvVar2 = (void *)InterlockedCompareExchange((LONG *)&DAT_00504b14,(LONG)Exchange,0);
    if (pvVar2 == (void *)0x0) {
LAB_004dc5ad:
      if (DAT_00504b10 == 1) {
        _amsg_exit(0x1f);
      }
      else if (DAT_00504b10 == 0) {
        DAT_00504b10 = 1;
        iVar4 = initterm_e(&DAT_004de5a4,&DAT_004de5b4);
        if (iVar4 != 0) {
          return 0xff;
        }
      }
      else {
        DAT_00502e38 = 1;
      }
      if (DAT_00504b10 == 1) {
        initterm(&DAT_004de598,&DAT_004de5a0);
        DAT_00504b10 = 2;
      }
      if (!bVar5) {
        InterlockedExchange((LONG *)&DAT_00504b14,0);
      }
      if ((DAT_00504b20 != (code *)0x0) &&
         (BVar3 = __IsNonwritableInCurrentImage((PBYTE)&DAT_00504b20), BVar3 != 0)) {
        (*DAT_00504b20)(0,2,0);
      }
      pbVar6 = *(byte **)_acmdln_exref;
      while ((bVar1 = *pbVar6, local_24 = pbVar6, 0x20 < bVar1 || ((bVar1 != 0 && (local_20 != 0))))
            ) {
        if (bVar1 == 0x22) {
          local_20 = (uint)(local_20 == 0);
        }
        iVar4 = _ismbblead((uint)bVar1);
        if (iVar4 != 0) {
          pbVar6 = pbVar6 + 1;
        }
        pbVar6 = pbVar6 + 1;
      }
      for (; (*local_24 != 0 && (*local_24 < 0x21)); local_24 = local_24 + 1) {
      }
      DAT_00502e34 = FUN_004dd240();
      if (DAT_00502e28 != 0) {
        if (DAT_00502e38 == 0) {
          _cexit();
        }
        return DAT_00502e34;
      }
                    /* WARNING: Subroutine does not return */
      exit(DAT_00502e34);
    }
    if (pvVar2 == Exchange) {
      bVar5 = true;
      goto LAB_004dc5ad;
    }
    Sleep(1000);
  } while( true );
}



/* ============================================================
   WinMain wrapper (0x4dd240) - SDL bootstrap
   Decompiled by REA 6.7.0 (Ghidra 12.1.4 backend)
   ============================================================ */


/* WARNING: Function: __alloca_probe_16 replaced with injection: alloca_probe */
/* WARNING: Unable to track spacebase fully for stack */

void FUN_004dd240(void)

{
  undefined1 *puVar1;
  int iVar2;
  size_t sVar3;
  undefined4 *puVar4;
  undefined4 auStackY_3c [3];
  undefined1 *local_2c;
  undefined1 *local_28;
  uint local_24;
  HMODULE local_20;
  char *local_1c;
  LPSTR local_18;
  undefined1 *local_14;
  undefined1 *local_10;
  undefined1 *local_c;
  int local_8;
  
  local_24 = DAT_00500030 ^ (uint)&stack0xfffffffc;
  auStackY_3c[2] = 0x4dd25b;
  local_20 = LoadLibraryA("DDRAW.DLL");
  if (local_20 != (HMODULE)0x0) {
    auStackY_3c[2] = 0x4dd26e;
    FreeLibrary(local_20);
  }
  auStackY_3c[2] = 0x4dd278;
  local_1c = (char *)SDL_getenv();
  if (local_1c == (char *)0x0) {
    FUN_004dd550();
  }
  else {
    auStackY_3c[2] = 0x4dd28e;
    iVar2 = atoi(local_1c);
    if (iVar2 != 0) {
      FUN_004dd550();
    }
  }
  local_18 = GetCommandLineA();
  auStackY_3c[2] = 0x4dd2b3;
  sVar3 = strlen(local_18);
  local_c = (undefined1 *)(sVar3 + 1);
  iVar2 = -(int)local_c;
  local_28 = (undefined1 *)((int)&local_2c + iVar2);
  local_14 = (undefined1 *)((int)&local_2c + iVar2);
  if (&local_2c == (undefined1 **)local_c) {
    *(undefined4 *)(&stack0xffffffd0 + iVar2) = 0x4dd2d8;
    FUN_004dd110();
  }
  else {
    local_28 = (undefined1 *)((int)&local_2c + iVar2);
    local_14 = (undefined1 *)((int)&local_2c + iVar2);
    *(undefined1 **)(&stack0xffffffd0 + iVar2) = local_c;
    *(LPSTR *)((int)auStackY_3c + iVar2 + 8) = local_18;
    *(undefined1 **)((int)auStackY_3c + iVar2 + 4) = local_14;
    puVar4 = (undefined4 *)((int)auStackY_3c + iVar2);
    *(undefined4 *)((int)auStackY_3c + iVar2) = 0x4dd2eb;
    SDL_strlcpy();
    puVar4[2] = 0;
    puVar4[1] = local_14;
    *puVar4 = 0x4dd2f9;
    local_8 = FUN_004dd360((char *)puVar4[1],puVar4[2]);
    puVar1 = (undefined1 *)(local_8 * 4 + 4);
    puVar4[2] = 0x4dd30e;
    iVar2 = -(int)puVar1;
    local_2c = (undefined1 *)((int)puVar4 + iVar2 + 0xc);
    local_10 = (undefined1 *)((int)puVar4 + iVar2 + 0xc);
    if (puVar4 + 3 == (undefined4 *)puVar1) {
      *(undefined4 *)((int)puVar4 + iVar2 + 8) = 0x4dd322;
      FUN_004dd110();
    }
    else {
      *(int *)((int)puVar4 + iVar2 + 8) = (int)puVar4 + iVar2 + 0xc;
      *(undefined1 **)((int)puVar4 + iVar2 + 4) = local_14;
      *(undefined4 *)((int)puVar4 + iVar2) = 0x4dd331;
      FUN_004dd360(*(char **)((int)puVar4 + iVar2 + 4),*(int *)((int)puVar4 + iVar2 + 8));
      *(undefined1 **)((int)puVar4 + iVar2 + 8) = local_10;
      *(int *)((int)puVar4 + iVar2 + 4) = local_8;
      *(undefined4 *)((int)puVar4 + iVar2) = 0x4dd341;
      FUN_004dcf80(*(undefined4 *)((int)puVar4 + iVar2 + 4),
                   *(undefined4 **)((int)puVar4 + iVar2 + 8));
    }
  }
  __security_check_cookie(local_24 ^ (uint)&stack0xfffffffc);
  return;
}



/* ============================================================
   SDL main (0x4dcf80) - SDL_Init + engine handoff
   Decompiled by REA 6.7.0 (Ghidra 12.1.4 backend)
   ============================================================ */


/* WARNING: Function: __alloca_probe_16 replaced with injection: alloca_probe */
/* WARNING: Unable to track spacebase fully for stack */

void __cdecl FUN_004dcf80(undefined4 param_1,undefined4 *param_2)

{
  int iVar1;
  undefined4 uVar2;
  HMODULE pHVar3;
  undefined1 *puVar4;
  undefined4 *puVar5;
  int *piVar6;
  undefined4 auStackY_2c [2];
  undefined1 *local_1c;
  uint local_18;
  undefined4 local_14;
  char *local_10;
  size_t local_c;
  char *local_8;
  
  local_18 = DAT_00500030 ^ (uint)&stack0xfffffffc;
  local_8 = (char *)*param_2;
  auStackY_2c[1] = 0x4dcfa6;
  local_10 = strrchr((char *)*param_2,0x5c);
  if (local_10 == (char *)0x0) {
    auStackY_2c[1] = 0x4dcfcb;
    local_10 = strrchr((char *)*param_2,0x2f);
    if (local_10 != (char *)0x0) {
      local_8 = local_10 + 1;
    }
  }
  else {
    local_8 = local_10 + 1;
  }
  auStackY_2c[1] = 0x4dcfec;
  local_10 = strrchr(local_8,0x2e);
  if (local_10 == (char *)0x0) {
    local_c = strlen(local_8);
  }
  else {
    local_c = (int)local_10 - (int)local_8;
  }
  iVar1 = -(int)(local_c + 1);
  local_1c = (undefined1 *)((int)&local_1c + iVar1);
  local_10 = (char *)((int)&local_1c + iVar1);
  if (&local_1c == (undefined1 **)(local_c + 1)) {
    *(undefined4 *)(&stack0xffffffe0 + iVar1) = 0x4dd031;
    FUN_004dd110();
  }
  else {
    local_1c = (undefined1 *)((int)&local_1c + iVar1);
    local_10 = (char *)((int)&local_1c + iVar1);
    *(size_t *)(&stack0xffffffe0 + iVar1) = local_c + 1;
    *(char **)(&stack0xffffffdc + iVar1) = local_8;
    *(char **)((int)auStackY_2c + iVar1 + 4) = local_10;
    puVar4 = (undefined1 *)((int)auStackY_2c + iVar1);
    *(undefined4 *)((int)auStackY_2c + iVar1) = 0x4dd04a;
    SDL_strlcpy();
    local_8 = local_10;
    *(undefined4 *)(puVar4 + 8) = 0x100000;
    puVar5 = (undefined4 *)(puVar4 + 4);
    *(undefined4 *)(puVar4 + 4) = 0x4dd05d;
    iVar1 = SDL_Init();
    if (-1 < iVar1) {
      *puVar5 = FUN_004dd140;
      puVar5[-1] = 0x4dd085;
      _atexit((_func_4879 *)*puVar5);
      *puVar5 = FUN_004dd130;
      puVar5[-1] = 0x4dd092;
      _atexit((_func_4879 *)*puVar5);
      *puVar5 = 0;
      puVar5[-1] = 0x4dd09d;
      pHVar3 = GetModuleHandleA((LPCSTR)*puVar5);
      *puVar5 = pHVar3;
      piVar6 = puVar5 + -1;
      puVar5[-1] = 0x4dd0a3;
      SDL_SetModuleHandle();
      *piVar6 = (int)param_2;
      piVar6[-1] = param_1;
      piVar6[-2] = 0x4dd0b3;
      local_14 = FUN_00421500(piVar6[-1],*piVar6);
      *piVar6 = local_14;
                    /* WARNING: Subroutine does not return */
      piVar6[-1] = (int)&UNK_004dd0c3;
      exit(*piVar6);
    }
    *puVar5 = 0x4dd069;
    uVar2 = SDL_GetError();
    *(undefined4 *)((int)puVar5 + -4) = uVar2;
    *(char **)((int)puVar5 + -8) = "WinMain() error";
    *(undefined4 *)((int)puVar5 + -0xc) = 0x4dd074;
    FUN_004dd0e0(*(undefined4 *)((int)puVar5 + -8),*(undefined4 *)((int)puVar5 + -4));
  }
  __security_check_cookie(local_18 ^ (uint)&stack0xfffffffc);
  return;
}



/* ============================================================
   MUGEN game main (0x421500) - init + gameflow loop
   Decompiled by REA 6.7.0 (Ghidra 12.1.4 backend)
   ============================================================ */


undefined4 __cdecl FUN_00421500(int param_1,int param_2)

{
  undefined4 extraout_ECX;
  undefined4 extraout_EDX;
  undefined8 uVar1;
  
  DAT_005040ec = (int)malloc(0x640);
  if ((void *)DAT_005040ec != (void *)0x0) {
    FUN_0040c3f0();
    FUN_0040c4a0("Parsing command line...\n");
    FUN_0040b070(param_1,param_2);
    FUN_0040c4a0("Parse command line OK\n");
    FUN_0040c4a0("Initializing...\n");
    FUN_0041a560();
    FUN_0040c4a0("Initialize OK\n");
    FUN_0040c4a0("Set up graphics...");
    FUN_0046ba90(0);
    (*(code *)PTR_FUN_00500b8c)(0);
    FUN_0046bfc0(DAT_005050e8,0,0);
    FUN_0040c4a0("timer...");
    FUN_00476ec0();
    FUN_00476ff0();
    uVar1 = FUN_00476d70();
    *(int *)(DAT_005040e8 + 0x130b4) = (int)uVar1;
    FUN_0040c4a0("OK\n");
    if (*(char *)(DAT_005040ec + 0x600) == '\0') {
      if (*(int *)(DAT_005040e8 + 0x130c8) != 0) {
        FUN_0040c4a0("Playing quick VS.\n");
      }
      FUN_0040c4a0("\nEntering gameflow loop");
      FUN_0040c4a0("\n");
      FUN_004124a0();
      if (DAT_005040c4 != 0) {
        free((void *)DAT_005040c4);
        DAT_005040c4 = 0;
      }
      FUN_0040c4a0("Returned from gameflow loop");
      FUN_0040c4a0("\n");
    }
    else {
      FUN_00428850(extraout_ECX,extraout_EDX,(char *)(DAT_005040ec + 0x600));
    }
    FUN_0040c4a0("Deinitializing...\n");
    if ((DAT_005040e8 != (char *)0x0) && (*DAT_005040e8 != '\0')) {
      _chdir(DAT_005040e8);
    }
    FUN_0041aea0();
    free((void *)DAT_005040ec);
    FUN_0040c4a0("Successful program termination.\n");
    if (DAT_00504094 != 0) {
      FUN_0040c4a0("\nLog file successfully closed.\n");
      FUN_0046c270();
      DAT_00504094 = 0;
    }
  }
  return 0;
}

