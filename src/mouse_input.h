#pragma once

#include "makcu_mouse.h"
#include <string>
#include <vector>

namespace MouseInput {

inline bool connect() {
  auto ports = MakcuDevice::listComPorts();
  if (ports.empty())
    return false;
  return MakcuDevice::connect(ports[0]);
}

inline void disconnect() { MakcuDevice::disconnect(); }

inline bool isConnected() { return MakcuDevice::connected; }

inline void moveRelative(int dx, int dy) { MakcuDevice::move(dx, dy); }

}
