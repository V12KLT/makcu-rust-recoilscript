#include "serialport.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <cfgmgr32.h>
#include <devguid.h>
#include <setupapi.h>

#pragma comment(lib, "setupapi.lib")
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <libudev.h>
#include <poll.h>
#include <regex>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#endif

namespace makcu {

SerialPort::SerialPort()
    : m_baudRate(115200), m_timeout(100), m_isOpen(false)
#ifdef _WIN32
      ,
      m_handle(INVALID_HANDLE_VALUE)
#else
      ,
      m_fd(-1)
#endif
{
#ifdef _WIN32
  memset(&m_dcb, 0, sizeof(m_dcb));
  memset(&m_timeouts, 0, sizeof(m_timeouts));
#endif
}

SerialPort::~SerialPort() { close(); }

bool SerialPort::open(const std::string &port, uint32_t baudRate) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_isOpen) {
    close();
  }

  m_portName = port;
  m_baudRate = baudRate;

#ifdef _WIN32
  std::string devicePath = "\\\\.\\" + port;
#else
  std::string devicePath = "/dev/" + port;
#endif

  if (!platformOpen(devicePath)) {
    return false;
  }

  if (!platformConfigurePort()) {
    platformClose();
    return false;
  }

  m_isOpen = true;

  m_stopListener = false;
  m_listenerThread = std::thread(&SerialPort::listenerLoop, this);

  return true;
}

void SerialPort::close() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_isOpen) {
    return;
  }

  m_stopListener.store(true, std::memory_order_release);

  if (m_listenerThread.joinable()) {

    auto future =
        std::async(std::launch::async, [this]() { m_listenerThread.join(); });

    if (future.wait_for(std::chrono::milliseconds(1000)) ==
        std::future_status::timeout) {

      m_listenerThread.detach();
    }
  }

  std::vector<std::unique_ptr<PendingCommand>> commandsToCancel;
  {
    std::lock_guard<std::mutex> cmdLock(m_commandMutex);
    commandsToCancel.reserve(m_pendingCommands.size());
    for (auto &[id, cmd] : m_pendingCommands) {
      commandsToCancel.push_back(std::move(cmd));
    }
    m_pendingCommands.clear();
  }

  for (auto &cmd : commandsToCancel) {
    try {
      cmd->promise.set_exception(
          std::make_exception_ptr(std::runtime_error("Connection closed")));
    } catch (...) {

    }
  }

  platformClose();
  m_isOpen.store(false, std::memory_order_release);

  m_lastButtonMask.store(0, std::memory_order_release);
}

bool SerialPort::isOpen() const { return m_isOpen; }

bool SerialPort::isActuallyConnected() const {
  if (!m_isOpen) {
    return false;
  }

#ifdef _WIN32

  if (m_handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  DCB dcb;
  return GetCommState(m_handle, &dcb) != 0;
#else

  if (m_fd < 0) {
    return false;
  }

  struct pollfd pfd;
  pfd.fd = m_fd;
  pfd.events = POLLERR | POLLHUP | POLLNVAL;
  pfd.revents = 0;

  int result = poll(&pfd, 1, 0);

  if (result < 0) {
    return false;
  }

  if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
    return false;
  }

  return true;
#endif
}

std::future<std::string>
SerialPort::sendTrackedCommand(const std::string &command, bool expectResponse,
                               std::chrono::milliseconds timeout) {

  if (!m_isOpen.load(std::memory_order_acquire)) {
    std::promise<std::string> promise;
    promise.set_exception(
        std::make_exception_ptr(std::runtime_error("Port not open")));
    return promise.get_future();
  }

  constexpr size_t MAX_COMMAND_LENGTH = 512;
  if (command.length() > MAX_COMMAND_LENGTH) {
    std::promise<std::string> promise;
    promise.set_exception(std::make_exception_ptr(
        std::runtime_error("Command too long (max " +
                           std::to_string(MAX_COMMAND_LENGTH) + " chars)")));
    return promise.get_future();
  }

  int cmdId = generateCommandId();
  auto pendingCmd =
      std::make_unique<PendingCommand>(cmdId, command, expectResponse, timeout);
  auto future = pendingCmd->promise.get_future();

  {
    std::lock_guard<std::mutex> lock(m_commandMutex);
    m_pendingCommands[cmdId] = std::move(pendingCmd);
  }

  std::string trackedCommand =
      expectResponse ? command + "#" + std::to_string(cmdId) + "\r"
                     : command + "\r";

  ssize_t bytesWritten =
      platformWrite(trackedCommand.c_str(), trackedCommand.length());

  if (bytesWritten != static_cast<ssize_t>(trackedCommand.length())) {
    std::lock_guard<std::mutex> lock(m_commandMutex);
    auto it = m_pendingCommands.find(cmdId);
    if (it != m_pendingCommands.end()) {
      try {
        std::string errorMsg = "Write failed";
        if (bytesWritten < 0) {
          errorMsg += " (" + getLastPlatformError() + ")";
        } else {
          errorMsg += " (partial write: " + std::to_string(bytesWritten) + "/" +
                      std::to_string(trackedCommand.length()) + " bytes)";
        }
        it->second->promise.set_exception(
            std::make_exception_ptr(std::runtime_error(errorMsg)));
      } catch (...) {

      }
      m_pendingCommands.erase(it);
    }
  }

  platformFlush();

  return future;
}

bool SerialPort::sendCommand(const std::string &command) {

  if (!m_isOpen.load(std::memory_order_acquire)) {
    return false;
  }

  constexpr size_t MAX_COMMAND_LENGTH = 512;
  if (command.length() > MAX_COMMAND_LENGTH) {
#ifdef DEBUG
    std::cerr << "SerialPort: Command too long (" << command.length() << " > "
              << MAX_COMMAND_LENGTH << ")" << std::endl;
#endif
    return false;
  }

  std::string fullCommand = command + "\r";

  ssize_t bytesWritten =
      platformWrite(fullCommand.c_str(), fullCommand.length());
  if (bytesWritten == static_cast<ssize_t>(fullCommand.length())) {
    return platformFlush();
  }

  return false;
}

void SerialPort::listenerLoop() {

  std::vector<uint8_t> readBuffer(BUFFER_SIZE);
  std::vector<uint8_t> lineBuffer(LINE_BUFFER_SIZE);
  size_t linePos = 0;

  auto lastCleanup = std::chrono::steady_clock::now();
  constexpr auto cleanupInterval = std::chrono::milliseconds(50);

  while (!m_stopListener && m_isOpen.load()) {
    try {

      size_t bytesAvailable = platformBytesAvailable();
      if (bytesAvailable == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        continue;
      }

      size_t bytesToRead =
          std::min<size_t>(bytesAvailable, static_cast<size_t>(BUFFER_SIZE));
      ssize_t bytesRead = platformRead(readBuffer.data(), bytesToRead);

      if (bytesRead <= 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      bool expectButtonMask = false;

      for (ssize_t i = 0; i < bytesRead; ++i) {
        uint8_t byte = readBuffer[i];

        if (expectButtonMask) {
          if (byte < 32) {
            handleButtonData(byte);
          }
          expectButtonMask = false;
          continue;
        }

        if (byte == 'k') {
          linePos = 0;
          lineBuffer[linePos++] = byte;
          continue;
        }

        if (linePos == 1 && byte == 'm') {
          lineBuffer[linePos++] = byte;
          continue;
        }

        if (linePos == 2 && byte == '.') {
          expectButtonMask = true;
          linePos = 0;
          continue;
        }

        if (byte == 0x0A) {
          if (linePos > 0) {
            std::string line(lineBuffer.begin(), lineBuffer.begin() + linePos);
            linePos = 0;
            if (!line.empty()) {
              processResponse(line);
            }
          }
        } else if (byte != 0x0D) {
          if (linePos < LINE_BUFFER_SIZE - 1) {
            lineBuffer[linePos++] = byte;
          } else {
            linePos = 0;
          }
        }
      }

      auto now = std::chrono::steady_clock::now();
      if (now - lastCleanup > cleanupInterval) {
        cleanupTimedOutCommands();
        lastCleanup = now;
      }

    } catch (const std::exception &e) {
      (void)e;
#ifdef DEBUG
      std::cerr << "SerialPort listener exception: " << e.what() << std::endl;
#endif

      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      if (!m_isOpen.load(std::memory_order_acquire)) {

        break;
      }
    } catch (...) {

#ifdef DEBUG
      std::cerr << "SerialPort listener unknown exception" << std::endl;
#endif

      std::this_thread::sleep_for(std::chrono::milliseconds(50));

      if (!m_isOpen.load(std::memory_order_acquire)) {

        break;
      }
    }
  }
}

void SerialPort::handleButtonData(uint8_t data) {
  uint8_t lastMask = m_lastButtonMask.load();
  if (data == lastMask) {
    return;
  }

  m_lastButtonMask.store(data);

  if (m_buttonCallback) {

    uint8_t changedBits = data ^ lastMask;
    for (int bit = 0; bit < 5; ++bit) {
      if (changedBits & (1 << bit)) {
        bool isPressed = data & (1 << bit);
        try {
          m_buttonCallback(bit, isPressed);
        } catch (...) {

        }
      }
    }
  }
}

void SerialPort::processResponse(const std::string &response) {

  std::string content = response;
  if (content.substr(0, 4) == ">>> ") {
    content = content.substr(4);
  }

  size_t hashPos = content.find('#');
  if (hashPos != std::string::npos) {

    std::string idStr = content.substr(hashPos + 1);
    size_t colonPos = idStr.find(':');
    if (colonPos != std::string::npos) {
      try {
        int cmdId = std::stoi(idStr.substr(0, colonPos));
        std::string result = idStr.substr(colonPos + 1);

        std::lock_guard<std::mutex> lock(m_commandMutex);
        auto it = m_pendingCommands.find(cmdId);
        if (it != m_pendingCommands.end()) {
          try {
            it->second->promise.set_value(result);
          } catch (...) {

          }
          m_pendingCommands.erase(it);
        }
        return;
      } catch (...) {

      }
    }
  }

  std::lock_guard<std::mutex> lock(m_commandMutex);
  if (!m_pendingCommands.empty()) {
    auto it = m_pendingCommands.begin();
    try {
      it->second->promise.set_value(content);
    } catch (...) {

    }
    m_pendingCommands.erase(it);
  }
}

void SerialPort::cleanupTimedOutCommands() {
  auto now = std::chrono::steady_clock::now();

  std::lock_guard<std::mutex> lock(m_commandMutex);
  auto it = m_pendingCommands.begin();
  while (it != m_pendingCommands.end()) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - it->second->timestamp);

    if (elapsed > it->second->timeout) {
      try {
        it->second->promise.set_exception(
            std::make_exception_ptr(std::runtime_error("Command timeout")));
      } catch (...) {

      }
      it = m_pendingCommands.erase(it);
    } else {
      ++it;
    }
  }
}

int SerialPort::generateCommandId() {
  return (m_commandCounter.fetch_add(1) % 10000) + 1;
}

void SerialPort::setButtonCallback(ButtonCallback callback) {
  m_buttonCallback = callback;
}

bool SerialPort::setBaudRate(uint32_t baudRate) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_baudRate = baudRate;

  if (m_isOpen) {

    return platformConfigurePort();
  }
  return true;
}

uint32_t SerialPort::getBaudRate() const { return m_baudRate; }

std::string SerialPort::getPortName() const { return m_portName; }

bool SerialPort::write(const std::vector<uint8_t> &data) {
  if (!m_isOpen.load())
    return false;
  ssize_t bytesWritten = platformWrite(data.data(), data.size());
  if (bytesWritten == static_cast<ssize_t>(data.size())) {
    return platformFlush();
  }
  return false;
}

bool SerialPort::write(const std::string &data) { return sendCommand(data); }

std::vector<uint8_t> SerialPort::read(size_t maxBytes) {

  std::vector<uint8_t> buffer;
  if (!m_isOpen || maxBytes == 0) {
    return buffer;
  }

  buffer.resize(maxBytes);
  ssize_t bytesRead = platformRead(buffer.data(), maxBytes);
  if (bytesRead > 0) {
    buffer.resize(bytesRead);
  } else {
    buffer.clear();
  }

  return buffer;
}

std::string SerialPort::readString(size_t maxBytes) {
  auto data = read(maxBytes);
  return std::string(data.begin(), data.end());
}

size_t SerialPort::available() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_isOpen) {
    return 0;
  }

  return const_cast<SerialPort *>(this)->platformBytesAvailable();
}

bool SerialPort::flush() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_isOpen) {
    return false;
  }

  return platformFlush();
}

bool SerialPort::clearInputBuffer() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_isOpen)
    return false;
#ifdef _WIN32
  return PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_RXABORT) != 0;
#else
  return tcflush(m_fd, TCIFLUSH) == 0;
#endif
}

void SerialPort::setTimeout(uint32_t timeoutMs) {
  m_timeout = timeoutMs;
  if (m_isOpen) {
    platformUpdateTimeouts();
  }
}

uint32_t SerialPort::getTimeout() const { return m_timeout; }

std::vector<std::string> SerialPort::getAvailablePorts() {
  std::vector<std::string> ports;

#ifdef _WIN32
  HKEY hKey;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                    KEY_READ, &hKey) == ERROR_SUCCESS) {
    char valueName[256];
    char data[256];
    DWORD valueNameSize, dataSize, dataType;
    DWORD index = 0;

    while (true) {
      valueNameSize = sizeof(valueName);
      dataSize = sizeof(data);

      LONG result =
          RegEnumValueA(hKey, index++, valueName, &valueNameSize, nullptr,
                        &dataType, reinterpret_cast<BYTE *>(data), &dataSize);

      if (result == ERROR_NO_MORE_ITEMS) {
        break;
      }

      if (result == ERROR_SUCCESS && dataType == REG_SZ) {
        ports.emplace_back(data);
      }
    }

    RegCloseKey(hKey);
  }
#else

  DIR *dir = opendir("/dev");
  if (dir) {
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
      std::string name(entry->d_name);
      if (name.substr(0, 6) == "ttyUSB" || name.substr(0, 6) == "ttyACM") {
        ports.push_back(name);
      }
    }
    closedir(dir);
  }
#endif

  std::sort(ports.begin(), ports.end());
  return ports;
}

std::vector<std::string> SerialPort::findMakcuPorts() {
  std::vector<std::string> makcuPorts;
#ifdef _WIN32
  auto allPorts = getAvailablePorts();
  HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr,
                                               nullptr, DIGCF_PRESENT);
  if (deviceInfoSet == INVALID_HANDLE_VALUE) {
    return makcuPorts;
  }

  SP_DEVINFO_DATA deviceInfoData;
  deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

  for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData);
       i++) {
    char description[256] = {0};
    char hardwareId[512] = {0};
    char portName[256] = {0};

    bool match = false;

    if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData,
                                          SPDRP_HARDWAREID, nullptr,
                                          reinterpret_cast<BYTE *>(hardwareId),
                                          sizeof(hardwareId), nullptr)) {
      std::string hwid(hardwareId);
      std::transform(hwid.begin(), hwid.end(), hwid.begin(), ::tolower);

      if (hwid.find("vid_1a86") != std::string::npos ||
          hwid.find("vid_2341") != std::string::npos ||
          hwid.find("vid_2a03") != std::string::npos ||
          hwid.find("vid_10c4") != std::string::npos ||
          hwid.find("vid_04d8") != std::string::npos ||
          hwid.find("vid_1b4f") != std::string::npos ||
          hwid.find("vid_0403") != std::string::npos) {
        match = true;
      }
    }

    if (!match && SetupDiGetDeviceRegistryPropertyA(
                      deviceInfoSet, &deviceInfoData, SPDRP_DEVICEDESC, nullptr,
                      reinterpret_cast<BYTE *>(description),
                      sizeof(description), nullptr)) {
      std::string desc(description);
      std::transform(desc.begin(), desc.end(), desc.begin(), ::tolower);

      if (desc.find("ch34") != std::string::npos ||
          desc.find("usb-enhanced-serial") != std::string::npos ||
          desc.find("usb-serial") != std::string::npos ||
          desc.find("usb serial") != std::string::npos ||
          desc.find("arduino") != std::string::npos ||
          desc.find("cp210") != std::string::npos ||
          desc.find("serial") != std::string::npos) {
        match = true;
      }
    }

    if (match) {
      HKEY hDeviceKey =
          SetupDiOpenDevRegKey(deviceInfoSet, &deviceInfoData, DICS_FLAG_GLOBAL,
                               0, DIREG_DEV, KEY_READ);
      if (hDeviceKey != INVALID_HANDLE_VALUE) {
        DWORD portNameSize = sizeof(portName);

        if (RegQueryValueExA(hDeviceKey, "PortName", nullptr, nullptr,
                             reinterpret_cast<BYTE *>(portName),
                             &portNameSize) == ERROR_SUCCESS) {
          std::string port(portName);
          if (std::find(allPorts.begin(), allPorts.end(), port) !=
              allPorts.end()) {
            makcuPorts.emplace_back(port);
          }
        }
        RegCloseKey(hDeviceKey);
      }
    }
  }

  SetupDiDestroyDeviceInfoList(deviceInfoSet);

  return makcuPorts;
#else

  struct udev *udev = udev_new();
  if (!udev) {
    return makcuPorts;
  }

  struct udev_enumerate *enumerate = udev_enumerate_new(udev);
  udev_enumerate_add_match_subsystem(enumerate, "tty");
  udev_enumerate_scan_devices(enumerate);

  struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
  struct udev_list_entry *entry;

  udev_list_entry_foreach(entry, devices) {
    const char *path = udev_list_entry_get_name(entry);
    struct udev_device *dev = udev_device_new_from_syspath(udev, path);

    if (dev) {
      struct udev_device *parent =
          udev_device_get_parent_with_subsystem_devtype(dev, "usb",
                                                        "usb_device");
      if (parent) {
        const char *idVendor =
            udev_device_get_sysattr_value(parent, "idVendor");
        const char *idProduct =
            udev_device_get_sysattr_value(parent, "idProduct");

        bool isMakcuDevice = false;

        if (idVendor && idProduct && strcmp(idVendor, "1a86") == 0 &&
            strcmp(idProduct, "55d3") == 0) {
          isMakcuDevice = true;
        }

        if (!isMakcuDevice) {
          const char *product =
              udev_device_get_sysattr_value(parent, "product");
          if (product) {
            std::string productStr(product);
            if (productStr.find("USB-Enhanced-SERIAL CH343") !=
                    std::string::npos ||
                productStr.find("USB-SERIAL CH340") != std::string::npos) {
              isMakcuDevice = true;
            }
          }
        }

        if (isMakcuDevice) {
          const char *devNode = udev_device_get_devnode(dev);
          if (devNode) {
            std::string portName =
                std::string(devNode).substr(5);
            makcuPorts.push_back(portName);
          }
        }
      }
      udev_device_unref(dev);
    }
  }

  udev_enumerate_unref(enumerate);
  udev_unref(udev);
#endif

  std::sort(makcuPorts.begin(), makcuPorts.end());
  makcuPorts.erase(std::unique(makcuPorts.begin(), makcuPorts.end()),
                   makcuPorts.end());
  return makcuPorts;
}

bool SerialPort::platformOpen(const std::string &devicePath) {
#ifdef _WIN32
  m_handle =
      CreateFileA(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  return m_handle != INVALID_HANDLE_VALUE;
#else
  m_fd = ::open(devicePath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  return m_fd >= 0;
#endif
}

void SerialPort::platformClose() {
#ifdef _WIN32
  if (m_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(m_handle);
    m_handle = INVALID_HANDLE_VALUE;
  }
#else
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
#endif
}

bool SerialPort::platformConfigurePort() {
#ifdef _WIN32
  m_dcb.DCBlength = sizeof(DCB);

  if (!GetCommState(m_handle, &m_dcb)) {
    return false;
  }

  m_dcb.BaudRate = m_baudRate;
  m_dcb.ByteSize = 8;
  m_dcb.Parity = NOPARITY;
  m_dcb.StopBits = ONESTOPBIT;
  m_dcb.fBinary = TRUE;
  m_dcb.fParity = FALSE;
  m_dcb.fOutxCtsFlow = FALSE;
  m_dcb.fOutxDsrFlow = FALSE;
  m_dcb.fDtrControl = DTR_CONTROL_DISABLE;
  m_dcb.fDsrSensitivity = FALSE;
  m_dcb.fTXContinueOnXoff = FALSE;
  m_dcb.fOutX = FALSE;
  m_dcb.fInX = FALSE;
  m_dcb.fErrorChar = FALSE;
  m_dcb.fNull = FALSE;
  m_dcb.fRtsControl = RTS_CONTROL_DISABLE;
  m_dcb.fAbortOnError = FALSE;

  if (!SetCommState(m_handle, &m_dcb)) {
    return false;
  }

  platformUpdateTimeouts();
  return true;
#else

  if (tcgetattr(m_fd, &m_oldTermios) != 0) {
    return false;
  }

  m_newTermios = m_oldTermios;

  m_newTermios.c_cflag &= ~PARENB;
  m_newTermios.c_cflag &= ~CSTOPB;
  m_newTermios.c_cflag &= ~CSIZE;
  m_newTermios.c_cflag |= CS8;
  m_newTermios.c_cflag &= ~CRTSCTS;

  m_newTermios.c_cflag |=
      CREAD |
      CLOCAL;

  m_newTermios.c_lflag &= ~(ICANON | ECHO | ECHOE |
                            ISIG);
  m_newTermios.c_lflag &=
      ~(ECHOK | ECHONL | IEXTEN);

  m_newTermios.c_oflag &= ~OPOST;
  m_newTermios.c_oflag &=
      ~(ONLCR | OCRNL | ONOCR | ONLRET);

  m_newTermios.c_iflag &=
      ~(IXON | IXOFF |
        IXANY);
  m_newTermios.c_iflag &= ~(INLCR | ICRNL | IGNCR);
  m_newTermios.c_iflag &=
      ~(ISTRIP | INPCK);
  m_newTermios.c_iflag &= ~(BRKINT | IGNBRK);

  m_newTermios.c_cc[VMIN] = 0;
  m_newTermios.c_cc[VTIME] = 1;

  speed_t speed;
  switch (m_baudRate) {
  case 9600:
    speed = B9600;
    break;
  case 19200:
    speed = B19200;
    break;
  case 38400:
    speed = B38400;
    break;
  case 57600:
    speed = B57600;
    break;
  case 115200:
    speed = B115200;
    break;
  case 230400:
    speed = B230400;
    break;
  case 460800:
    speed = B460800;
    break;
  case 921600:
    speed = B921600;
    break;
  case 1000000:
    speed = B1000000;
    break;
  case 1152000:
    speed = B1152000;
    break;
  case 1500000:
    speed = B1500000;
    break;
  case 2000000:
    speed = B2000000;
    break;
  case 2500000:
    speed = B2500000;
    break;
  case 3000000:
    speed = B3000000;
    break;
  case 3500000:
    speed = B3500000;
    break;
  case 4000000:
    speed = B4000000;
    break;
  default:
    speed = B115200;
    break;
  }

  cfsetispeed(&m_newTermios, speed);
  cfsetospeed(&m_newTermios, speed);

  if (tcsetattr(m_fd, TCSANOW, &m_newTermios) != 0) {
    return false;
  }

  tcflush(m_fd, TCIOFLUSH);

  return true;
#endif
}

void SerialPort::platformUpdateTimeouts() {
#ifdef _WIN32

  m_timeouts.ReadIntervalTimeout = 1;
  m_timeouts.ReadTotalTimeoutConstant = 10;
  m_timeouts.ReadTotalTimeoutMultiplier = 1;
  m_timeouts.WriteTotalTimeoutConstant = 10;
  m_timeouts.WriteTotalTimeoutMultiplier = 1;

  SetCommTimeouts(m_handle, &m_timeouts);
#else

  if (m_isOpen && m_fd >= 0) {
    struct termios currentTermios;
    if (tcgetattr(m_fd, &currentTermios) == 0) {

      uint8_t vtime =
          std::min(255, std::max(1, static_cast<int>(m_timeout / 100)));
      currentTermios.c_cc[VTIME] = vtime;
      currentTermios.c_cc[VMIN] = 0;
      tcsetattr(m_fd, TCSANOW, &currentTermios);
    }
  }
#endif
}

ssize_t SerialPort::platformWrite(const void *data, size_t length) {
#ifdef _WIN32
  DWORD bytesWritten = 0;
  bool success = WriteFile(m_handle, data, static_cast<DWORD>(length),
                           &bytesWritten, nullptr);
  return success ? static_cast<ssize_t>(bytesWritten) : -1;
#else
  return ::write(m_fd, data, length);
#endif
}

ssize_t SerialPort::platformRead(void *buffer, size_t maxBytes) {
#ifdef _WIN32
  DWORD bytesRead = 0;
  bool success = ReadFile(m_handle, buffer, static_cast<DWORD>(maxBytes),
                          &bytesRead, nullptr);
  return success ? static_cast<ssize_t>(bytesRead) : -1;
#else
  return ::read(m_fd, buffer, maxBytes);
#endif
}

size_t SerialPort::platformBytesAvailable() {
#ifdef _WIN32
  COMSTAT comStat;
  DWORD errors;
  if (ClearCommError(m_handle, &errors, &comStat)) {
    return comStat.cbInQue;
  }
  return 0;
#else
  int bytesAvailable = 0;
  if (ioctl(m_fd, FIONREAD, &bytesAvailable) >= 0) {
    return static_cast<size_t>(bytesAvailable);
  }
  return 0;
#endif
}

bool SerialPort::platformFlush() {
#ifdef _WIN32
  return FlushFileBuffers(m_handle) != 0;
#else
  return tcdrain(m_fd) == 0;
#endif
}

std::string SerialPort::getLastPlatformError() {
#ifdef _WIN32
  DWORD error = GetLastError();
  return "Windows error: " + std::to_string(error);
#else
  return "errno: " + std::to_string(errno);
#endif
}

}
