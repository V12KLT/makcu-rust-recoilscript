#include "makcu_mouse.h"
#include "makcu_sdk.h"
#include <cstdio>

static makcu::Device g_makcuDevice;

HANDLE MakcuDevice::serialHandle = INVALID_HANDLE_VALUE;
bool MakcuDevice::connected = false;
bool MakcuDevice::listening = false;
std::mutex MakcuDevice::serialMutex;
std::thread MakcuDevice::buttonThread;
bool MakcuDevice::buttonStates[6] = {false};
bool MakcuDevice::lmbWasPressed = false;
std::set<unsigned char> MakcuDevice::validBytes;

bool MakcuDevice::connect(const std::string &port) {
  printf("[MAKCU] Connecting via SDK to %s...\n", port.c_str());
  bool result = g_makcuDevice.connect(port);
  if (result) {
    connected = true;
    printf("[MAKCU] SDK connected successfully\n");

    g_makcuDevice.enableButtonMonitoring(true);
    g_makcuDevice.enableHighPerformanceMode(true);

    g_makcuDevice.setMouseButtonCallback(
        [](makcu::MouseButton button, bool pressed) {
          int idx = static_cast<int>(button);
          if (idx >= 0 && idx < 5) {

            buttonStates[idx + 1] = pressed;
          }
        });
  } else {
    printf("[MAKCU] SDK connection failed to %s\n", port.c_str());
  }
  return result;
}

void MakcuDevice::disconnect() {
  g_makcuDevice.disconnect();
  connected = false;
  listening = false;
  printf("[MAKCU] Disconnected\n");
}

std::string MakcuDevice::getVersion() {
  if (connected) {
    return g_makcuDevice.getVersion();
  }
  return "";
}

void MakcuDevice::move(int x, int y) {
  if (x == 0 && y == 0)
    return;
  if (connected) {
    g_makcuDevice.mouseMove(x, y);
  }
}

void MakcuDevice::moveSmooth(int x, int y, int segments) {
  if (x == 0 && y == 0)
    return;
  if (connected) {
    g_makcuDevice.mouseMoveSmooth(x, y, segments);
  }
}

void MakcuDevice::sendCommand(const char *cmd, size_t len) {
  if (connected) {
    std::string command(cmd, len);

    while (!command.empty() &&
           (command.back() == '\r' || command.back() == '\n'))
      command.pop_back();
    g_makcuDevice.sendRawCommand(command);
  }
}

void MakcuDevice::click(MouseButton button) {
  if (connected) {
    makcu::MouseButton mb;
    switch (button) {
    case MouseButton::LEFT:
      mb = makcu::MouseButton::LEFT;
      break;
    case MouseButton::RIGHT:
      mb = makcu::MouseButton::RIGHT;
      break;
    case MouseButton::MIDDLE:
      mb = makcu::MouseButton::MIDDLE;
      break;
    case MouseButton::MOUSE4:
      mb = makcu::MouseButton::SIDE1;
      break;
    case MouseButton::MOUSE5:
      mb = makcu::MouseButton::SIDE2;
      break;
    default:
      return;
    }
    g_makcuDevice.click(mb);
  }
}

void MakcuDevice::startButtonListener(bool rmbEnabled) {

  listening = true;
  printf("[+] Button listener started (RMB detection enabled)\n");
}

void MakcuDevice::readButtons() {

}

bool MakcuDevice::buttonPressed(MouseButton button) {

  int vk = 0;
  switch (button) {
  case MouseButton::LEFT:
    vk = VK_LBUTTON;
    break;
  case MouseButton::RIGHT:
    vk = VK_RBUTTON;
    break;
  case MouseButton::MIDDLE:
    vk = VK_MBUTTON;
    break;
  case MouseButton::MOUSE4:
    vk = VK_XBUTTON1;
    break;
  case MouseButton::MOUSE5:
    vk = VK_XBUTTON2;
    break;
  }
  bool winState = (GetAsyncKeyState(vk) & 0x8000) != 0;

  if (connected) {

    makcu::MouseButton mb;
    switch (button) {
    case MouseButton::LEFT:
      mb = makcu::MouseButton::LEFT;
      break;
    case MouseButton::RIGHT:
      mb = makcu::MouseButton::RIGHT;
      break;
    case MouseButton::MIDDLE:
      mb = makcu::MouseButton::MIDDLE;
      break;
    case MouseButton::MOUSE4:
      mb = makcu::MouseButton::SIDE1;
      break;
    case MouseButton::MOUSE5:
      mb = makcu::MouseButton::SIDE2;
      break;
    default:
      return winState;
    }
    if (g_makcuDevice.mouseButtonState(mb))
      return true;
  }

  return winState;
}

void MakcuDevice::press(MouseButton button, int pressState) {
  if (!connected)
    return;
  makcu::MouseButton mb;
  switch (button) {
  case MouseButton::LEFT:
    mb = makcu::MouseButton::LEFT;
    break;
  case MouseButton::RIGHT:
    mb = makcu::MouseButton::RIGHT;
    break;
  case MouseButton::MIDDLE:
    mb = makcu::MouseButton::MIDDLE;
    break;
  case MouseButton::MOUSE4:
    mb = makcu::MouseButton::SIDE1;
    break;
  case MouseButton::MOUSE5:
    mb = makcu::MouseButton::SIDE2;
    break;
  default:
    return;
  }
  if (pressState)
    g_makcuDevice.mouseDown(mb);
  else
    g_makcuDevice.mouseUp(mb);
}

std::vector<std::string> MakcuDevice::listComPorts() {
  auto devices = makcu::Device::findDevices();
  std::vector<std::string> ports;
  for (auto &d : devices)
    ports.push_back(d.port);
  return ports;
}

void MakcuDevice::unlockAllButtons() {
  if (connected) {
    g_makcuDevice.lockMouseLeft(false);
    g_makcuDevice.lockMouseRight(false);
    g_makcuDevice.lockMouseMiddle(false);
    g_makcuDevice.lockMouseSide1(false);
    g_makcuDevice.lockMouseSide2(false);
  }
}
