#pragma once

#ifdef _WIN32
    #ifdef MAKCU_EXPORTS
        #define MAKCU_API __declspec(dllexport)
    #elif defined(MAKCU_SHARED)
        #define MAKCU_API __declspec(dllimport)
    #else
        #define MAKCU_API
    #endif
#else
    #ifdef __GNUC__
        #define MAKCU_API __attribute__((visibility("default")))
    #else
        #define MAKCU_API
    #endif
#endif

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <exception>
#include <unordered_map>
#include <atomic>
#include <future>
#include <chrono>

namespace makcu {

    class SerialPort;

    enum class MouseButton : uint8_t {
        LEFT = 0,
        RIGHT = 1,
        MIDDLE = 2,
        SIDE1 = 3,
        SIDE2 = 4
    };

    enum class ConnectionStatus {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        CONNECTION_ERROR,
    };

    struct DeviceInfo {
        std::string port;
        std::string description;
        uint16_t vid;
        uint16_t pid;
        bool isConnected;
    };

    struct MouseButtonStates {
        bool left;
        bool right;
        bool middle;
        bool side1;
        bool side2;

        MouseButtonStates() : left(false), right(false), middle(false), side1(false), side2(false) {}

        bool operator[](MouseButton button) const {
            switch (button) {
            case MouseButton::LEFT: return left;
            case MouseButton::RIGHT: return right;
            case MouseButton::MIDDLE: return middle;
            case MouseButton::SIDE1: return side1;
            case MouseButton::SIDE2: return side2;
            }
            return false;
        }

        void set(MouseButton button, bool state) {
            switch (button) {
            case MouseButton::LEFT: left = state; break;
            case MouseButton::RIGHT: right = state; break;
            case MouseButton::MIDDLE: middle = state; break;
            case MouseButton::SIDE1: side1 = state; break;
            case MouseButton::SIDE2: side2 = state; break;
            }
        }
    };

    class MAKCU_API MakcuException : public std::exception {
    public:
        explicit MakcuException(const std::string& message) : m_message(message) {}
        const char* what() const noexcept override { return m_message.c_str(); }
    private:
        std::string m_message;
    };

    class MAKCU_API ConnectionException : public MakcuException {
    public:
        explicit ConnectionException(const std::string& message)
            : MakcuException("Connection error: " + message) {
        }
    };

    class MAKCU_API CommandException : public MakcuException {
    public:
        explicit CommandException(const std::string& message)
            : MakcuException("Command error: " + message) {
        }
    };

    class MAKCU_API TimeoutException : public MakcuException {
    public:
        explicit TimeoutException(const std::string& message)
            : MakcuException("Timeout error: " + message) {
        }
    };

    class MAKCU_API Device {
    public:

        using MouseButtonCallback = std::function<void(MouseButton, bool)>;
        using ConnectionCallback = std::function<void(bool)>;

        Device();
        ~Device();

        static std::vector<DeviceInfo> findDevices();
        static std::string findFirstDevice();

        bool connect(const std::string& port = "");
        void disconnect();
        bool isConnected() const;
        ConnectionStatus getStatus() const;

        std::future<bool> connectAsync(const std::string& port = "");

        DeviceInfo getDeviceInfo() const;
        std::string getVersion() const;

        bool mouseDown(MouseButton button);
        bool mouseUp(MouseButton button);
        bool click(MouseButton button);

        bool mouseButtonState(MouseButton button);

        bool mouseMove(int32_t x, int32_t y);
        bool mouseMoveSmooth(int32_t x, int32_t y, uint32_t segments);
        bool mouseMoveBezier(int32_t x, int32_t y, uint32_t segments,
            int32_t ctrl_x, int32_t ctrl_y);

        bool mouseDrag(MouseButton button, int32_t x, int32_t y);
        bool mouseDragSmooth(MouseButton button, int32_t x, int32_t y, uint32_t segments = 10);
        bool mouseDragBezier(MouseButton button, int32_t x, int32_t y, uint32_t segments = 20,
            int32_t ctrl_x = 0, int32_t ctrl_y = 0);

        bool mouseWheel(int32_t delta);

        bool lockMouseX(bool lock = true);
        bool lockMouseY(bool lock = true);
        bool lockMouseLeft(bool lock = true);
        bool lockMouseMiddle(bool lock = true);
        bool lockMouseRight(bool lock = true);
        bool lockMouseSide1(bool lock = true);
        bool lockMouseSide2(bool lock = true);

        bool isMouseXLocked() const;
        bool isMouseYLocked() const;
        bool isMouseLeftLocked() const;
        bool isMouseMiddleLocked() const;
        bool isMouseRightLocked() const;
        bool isMouseSide1Locked() const;
        bool isMouseSide2Locked() const;

        std::unordered_map<std::string, bool> getAllLockStates() const;

        uint8_t catchMouseLeft();
        uint8_t catchMouseMiddle();
        uint8_t catchMouseRight();
        uint8_t catchMouseSide1();
        uint8_t catchMouseSide2();

        bool enableButtonMonitoring(bool enable = true);
        bool isButtonMonitoringEnabled() const;
        uint8_t getButtonMask() const;

        std::string getMouseSerial();
        bool setMouseSerial(const std::string& serial);
        bool resetMouseSerial();

        bool setBaudRate(uint32_t baudRate, bool validateCommunication = true);

        void setMouseButtonCallback(MouseButtonCallback callback);
        void setConnectionCallback(ConnectionCallback callback);

        bool clickSequence(const std::vector<MouseButton>& buttons,
            std::chrono::milliseconds delay = std::chrono::milliseconds(50));
        bool movePattern(const std::vector<std::pair<int32_t, int32_t>>& points,
            bool smooth = true, uint32_t segments = 10);

        void enableHighPerformanceMode(bool enable = true);
        bool isHighPerformanceModeEnabled() const;

        class MAKCU_API BatchCommandBuilder {
        public:
            BatchCommandBuilder& move(int32_t x, int32_t y);
            BatchCommandBuilder& moveSmooth(int32_t x, int32_t y, uint32_t segments = 10);
            BatchCommandBuilder& moveBezier(int32_t x, int32_t y, uint32_t segments = 20,
                int32_t ctrl_x = 0, int32_t ctrl_y = 0);
            BatchCommandBuilder& click(MouseButton button);
            BatchCommandBuilder& press(MouseButton button);
            BatchCommandBuilder& release(MouseButton button);
            BatchCommandBuilder& scroll(int32_t delta);
            BatchCommandBuilder& drag(MouseButton button, int32_t x, int32_t y);
            BatchCommandBuilder& dragSmooth(MouseButton button, int32_t x, int32_t y, uint32_t segments = 10);
            BatchCommandBuilder& dragBezier(MouseButton button, int32_t x, int32_t y, uint32_t segments = 20,
                int32_t ctrl_x = 0, int32_t ctrl_y = 0);
            bool execute();

        private:
            friend class Device;
            BatchCommandBuilder(Device* device) : m_device(device) {}
            Device* m_device;
            std::vector<std::string> m_commands;
        };

        BatchCommandBuilder createBatch();

        bool sendRawCommand(const std::string& command) const;
        std::string receiveRawResponse() const;

    private:

        class Impl;
        std::unique_ptr<Impl> m_impl;

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;
    };

    MAKCU_API std::string mouseButtonToString(MouseButton button);
    MAKCU_API MouseButton stringToMouseButton(const std::string& buttonName);

    class MAKCU_API PerformanceProfiler {
    private:
        static std::atomic<bool> s_enabled;
        static std::mutex s_mutex;
        static std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> s_stats;

    public:
        static void enableProfiling(bool enable = true) {
            s_enabled.store(enable);
        }

        static void logCommandTiming(const std::string& command, std::chrono::microseconds duration) {
            if (!s_enabled.load()) return;

            std::lock_guard<std::mutex> lock(s_mutex);
            auto& [count, total_us] = s_stats[command];
            count++;
            total_us += duration.count();
        }

        static std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> getStats() {
            std::lock_guard<std::mutex> lock(s_mutex);
            return s_stats;
        }

        static void resetStats() {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_stats.clear();
        }
    };

}
