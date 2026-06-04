#include "gui/menu_globals.h"
#include "src/game_data.h"
#include "src/mouse_input.h"
#include "src/recoil_blatant.h"
#include "src/recoil_legit.h"
#include "src/security.h"
#include "src/settings_manager.h"
#include <Windows.h>
#include <cstdio>

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS *) {
  ExitProcess(1);
  return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  SetUnhandledExceptionFilter(CrashHandler);

  auto [safe, reason] = startSecurityCheck();
  if (!safe) {
    Sleep(1000);
    return 1;
  }

  initWeaponData();
  MouseInput::connect();
  loadSettings();
  ApplyTheme(g_gameState.themeIndex);

  {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    }
  }
  g_running = true;
  if (InitMainMenu()) {
    RenderMainMenu();
    CleanupMainMenu();
  }

  g_legitEngine.stop();
  g_blatantEngine.stop();
  MouseInput::disconnect();

  return 0;
}
