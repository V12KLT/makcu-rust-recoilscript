#pragma once
#include <Windows.h>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

enum class MouseButton {
  LEFT = 1,
  RIGHT = 2,
  MIDDLE = 3,
  MOUSE4 = 4,
  MOUSE5 = 5
};

class MakcuDevice {
public:
  static bool connected;
  static HANDLE serialHandle;
  static std::mutex serialMutex;
  static bool buttonStates[6];
  static bool lmbWasPressed;
  static std::thread buttonThread;
  static bool listening;
  static std::set<unsigned char> validBytes;

  static bool connect(const std::string &comPort);
  static void disconnect();
  static void move(int x, int y);
  static void moveSmooth(int x, int y, int segments = 10);
  static void click(MouseButton button);
  static void press(MouseButton button, int pressState);
  static bool buttonPressed(MouseButton button);
  static void unlockAllButtons();
  static std::string getVersion();
  static std::vector<std::string> listComPorts();

  static void startButtonListener(bool rmbEnabled = true);

private:
  static void sendCommand(const char *cmd, size_t len);
  static void readButtons();
};
