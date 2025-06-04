# 🔧 GhalibVR - Custom OpenVR Controller Driver

**GhalibVR** is a custom OpenVR controller driver that emulates a VR controller using data sent over UDP — ideal for IMU+ESP32-based DIY motion controllers 🧠🎮

## 🚀 Features

- ✅ Fully compatible with **SteamVR**
- 🎮 Custom input profile (`joystick`, `trigger`, `grip`, buttons)
- 📡 Receives real-time data via **UDP** (from ESP32 or other device)
- 🔁 Automatically updates controller input components
- 💡 Easy to configure and extend

---

## 📁 Project Structure

```
GhalibVR/
├── driver.vrdrivermanifest
├── bin/
│   └── win64/
│       └── driver DLL and dependencies
├── resources/
│   ├── input/
│   │   ├── controller_profile.json
│   │   └── binding_ghalibvrcontroller.json
├── src/
│   ├── ControllerDriver.cpp
│   └── ControllerDriver.h
└── README.md
```

---

## ⚙️ How It Works

1. Your ESP32 (or another device) sends controller data via **UDP** to your PC:
   ```
   format: roll,pitch,yaw,trigger,grip,joystickX,joystickY
   example: 0.1,0.2,0.3,0.5,0.6,0.7,0.8
   ```

2. The C++ driver listens on **port 4210** and parses this data.

3. **SteamVR** uses your `controller_profile.json` and `binding_ghalibvrcontroller.json` to map these inputs to in-game actions.

---

## 🧪 Testing

Make sure **SteamVR is installed and closed** during driver installation.

### ✅ Install the Driver

Copy your built driver folder to:

```
C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\ghalibvr\
```

Make sure the `driver.vrdrivermanifest` is at the root of this directory.

Then, register the driver using:

```bash
vrpathreg adddriver "C:\Path\To\ghalibvr"
```

### 🔁 Restart SteamVR

Once SteamVR is running:

- Open **SteamVR Settings > Controllers > Manage Controller Bindings**
- Select `GhalibVR` as your device
- Confirm bindings are loaded from `controller_profile.json`

---

## 📡 ESP32 Format Example

```cpp
// ESP32 pseudocode
String data = String(roll) + "," + pitch + "," + yaw + "," +
              trigger + "," + grip + "," +
              joystickX + "," + joystickY;
udp.send(data);
```

---

## 🛠️ Build Instructions

1. Use **Visual Studio 2022**
2. Link against:
   - `openvr_api.lib`
   - `ws2_32.lib`
3. Output your driver `.dll` to `bin/win64/`
4. Make sure your input files and `driver.vrdrivermanifest` are in the correct directories

---

## 📜 License

MIT License (or customize this as needed)
