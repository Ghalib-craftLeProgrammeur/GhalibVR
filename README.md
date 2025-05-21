# OpenVR Simple Controller with ESP32 IMU Integration (via TCP)

This project demonstrates an OpenVR driver (`simplecontroller_tcp_imu`) that simulates a VR controller whose orientation and basic inputs are driven by data received over a TCP/IP connection, presumably from an ESP32 with an IMU.

It's an extension of the standard `simplecontroller` sample from the OpenVR SDK.

## Features

*   **OpenVR Driver:** Implements a basic VR controller discoverable by SteamVR.
*   **TCP Server:** Each controller instance (left/right) runs a TCP server to listen for incoming data.
    *   Default Left Controller Port: `12345`
    *   Default Right Controller Port: `12346`
*   **IMU Data Integration:**
    *   Receives quaternion data for controller orientation.
    *   Receives button press states (e.g., 'A' button click, Trigger click).
    *   Receives analog trigger value.
*   **Fallback Pose:** If no IMU data is received, the controller's orientation defaults to identity, and its position remains relative to the HMD (as in the original `simplecontroller`).
*   **Threaded Architecture:** TCP server and pose updates run in separate threads to avoid blocking the main VR runtime.

## Project Structure (Key Files)

*   **`drivers/drivers/simplecontroller/`**: Root directory for this specific driver sample.
    *   **`src/`**:
        *   `controller_device_driver.h` / `.cpp`: The core logic for the controller device, including the TCP server implementation and IMU data handling.
        *   `device_provider.h` / `.cpp`: Manages the instantiation of controller devices.
        *   `hmd_driver_factory.cpp`: Entry point for the OpenVR runtime to load the driver.
        *   `vrmath.h`: Utility math functions (included from OpenVR samples).
    *   **`simplecontroller/`**: Resource files for the driver.
        *   `driver.vrdrivermanifest`: Tells SteamVR how to load the driver.
        *   `resources/input/mycontroller_profile.json`: Defines the controller's inputs for SteamVR.
        *   `resources/settings/default.vrsettings`: Default configuration for the driver.
*   **`send_test_data.py`**: A Python script to simulate an ESP32 client sending test data to the driver's TCP server.

## Prerequisites

*   **OpenVR SDK:** This driver is part of the OpenVR SDK samples.
*   **Build Environment:**
    *   CMake and a C++ compiler (e.g., MSVC on Windows, GCC/Clang on Linux/macOS).
    *   Visual Studio solution files are also typically provided with the OpenVR samples.
*   **(Windows) Winsock:** The TCP server uses Winsock. `Ws2_32.lib` should be linked (often handled by `#pragma comment(lib, "Ws2_32.lib")`).
*   **Python 3.x:** To run the `send_test_data.py` script.
*   **ESP32 with IMU (Optional):** If you intend to use a real ESP32, you'll need the ESP-IDF or Arduino environment for the ESP32, an IMU (like MPU6050 or BNO055), and code to read the IMU and send data over TCP.

## Building the Driver

1.  **Navigate** to the root of your OpenVR SDK samples directory (e.g., `openvr/samples/`).
2.  **Follow the build instructions** for the OpenVR samples (usually involving CMake or opening the provided Visual Studio solution).
    *   **CMake Example (from `samples` directory):**
        ```bash
        mkdir build
        cd build
        cmake ..
        cmake --build . --config Release  # Or Debug
        ```
    *   **Visual Studio:** Open `drivers/vs-openvr_samples.sln` (or similar) and build the `simplecontroller` project.
3.  The compiled driver (`driver_simplecontroller.dll` or equivalent) will be located in a build output directory, typically something like `samples/drivers/output/drivers/simplecontroller/bin/win64/`.

## TCP Data Protocol

The driver expects data from the TCP client (e.g., ESP32 or the Python script) in the following text-based format, terminated by a newline character (`\n`):

`qx,qy,qz,qw;btnA_click,btnTrig_click,trig_val`

Where:

*   `qx, qy, qz, qw`: Float values representing the quaternion for orientation.
*   `btnA_click`: Integer (0 for released, 1 for pressed) for the 'A' button click state.
*   `btnTrig_click`: Integer (0 for released, 1 for pressed) for the trigger's digital click state.
*   `trig_val`: Float value (0.0 to 1.0) for the analog trigger pull.

**Example:** `0.1234,-0.5678,0.0100,0.8123;1,0,0.75\n`

## Linking the Driver to SteamVR

1.  Locate your SteamVR installation directory (typically `C:\Program Files (x86)\Steam\steamapps\common\SteamVR`).
2.  Navigate to `bin\win64\` (or `bin/linux64/` or `bin/osx32/` depending on your OS).
3.  Run `vrpathreg.exe` (or `vrpathreg` on Linux/macOS) to register your driver:
    ```bash
    vrpathreg adddriver "path/to/your/openvr/samples/drivers/output/drivers/simplecontroller"
    ```
    Replace `"path/to/your/openvr/samples/drivers/output/drivers/simplecontroller"` with the actual absolute path to the directory containing your compiled `driver_simplecontroller.dll` and the `simplecontroller` resource folder.
4.  You can verify the registration by checking the `openvrpaths.vrpath` file located in:
    *   Windows: `%LOCALAPPDATA%\openvr\openvrpaths.vrpath`
    *   Linux: `~/.config/openvr/openvrpaths.vrpath`
    *   macOS: `~/Library/Application Support/OpenVR/.openvr/openvrpaths.vrpath`

## Running and Testing

1.  **Build and Link** the driver as described above.
2.  **Start SteamVR.**
    *   Check the SteamVR logs (`vrserver.txt` located in `Steam/logs/`) or the SteamVR Web Console (SteamVR Hamburger Menu > Developer > Web Console) for messages from the "simplecontroller" driver. You should see logs indicating the TCP server is starting for each controller instance (left/right) on their respective ports.
3.  **Run the Test Client (Python Script):**
    *   Open `send_test_data.py`.
    *   Modify the `HOST` (if SteamVR is on a different machine) and `PORT` (12345 for left, 12346 for right) variables as needed.
    *   Execute the script: `python send_test_data.py`
4.  **Observe in SteamVR:**
    *   The corresponding virtual controller in SteamVR should start rotating according to the data sent by the Python script.
    *   Button 'A' presses and trigger movements simulated by the script should be reflected in SteamVR's controller test UI or in VR applications that use your controller's bindings.
    *   The Python script will print "Connected..." and then will periodically send data. It will also print when it simulates a button A press.

## ESP32 Client (Conceptual)

If you are using an ESP32, your code would typically:

1.  Initialize WiFi and connect to your network.
2.  Initialize your IMU sensor.
3.  Read quaternion data and any button/trigger states from the IMU and GPIOs.
4.  Format this data into the string protocol defined above.
5.  Establish a TCP client connection to your PC's IP address and the correct port (`12345` or `12346`).
6.  Periodically send the formatted data string over the TCP socket.
7.  Handle disconnections and attempt to reconnect.

## Troubleshooting

*   **Driver Not Loading:**
    *   Ensure `vrpathreg` pointed to the correct directory containing the `driver.vrdrivermanifest` file and the `bin` subdirectory with your DLL.
    *   Check `vrserver.txt` for errors related to "simplecontroller".
    *   Verify your driver is enabled in SteamVR settings (Startup/Shutdown > Manage Add-ons).
*   **TCP Connection Refused (Python script):**
    *   Verify the `HOST` IP address and `PORT` in the Python script are correct.
    *   Ensure SteamVR is running and your driver has loaded (check logs for "TCP Server listening...").
    *   Check your PC's firewall. You might need to create an inbound rule for `vrserver.exe` or for the specific ports (12345, 12346).
*   **No Data Received by Driver:**
    *   Ensure your ESP32 (or Python script) is actually sending data in the correct format and is successfully connected.
    *   Add more `DriverLog` statements in `MyTCPServerThreadFunction` in `controller_device_driver.cpp` to see if data is being received and parsed.
*   **Incorrect Orientation/Inputs:**
    *   Double-check the data format being sent by the client matches what the driver's `sscanf_s` expects.
    *   Verify quaternion component order (WXYZ, XYZW, etc.) is consistent between your IMU/ESP32 and how the driver interprets it (OpenVR generally uses WXYZ or XYZW, ensure `vr::HmdQuaternion_t` matches your source). `vrmath.h` assumes W,X,Y,Z for `HmdQuaternion_t`.
*   **VR Freezing:** If your TCP server code (especially `accept` or `recv`) is not in a separate thread or has issues that block, it can freeze `vrserver.exe`.

## Further Development

*   **ESP32 Client Implementation:** Develop the actual ESP32 firmware.
*   **Positional Data:** Extend the protocol and driver to accept and use positional data from the ESP32 if your setup supports it (e.g., using additional sensors or optical tracking).
*   **More Inputs:** Add support for more buttons, joysticks, touchpads, etc.
*   **Haptics:** Implement sending haptic commands back to the ESP32 over TCP if it has a vibration motor.
*   **Robustness:** Improve error handling, automatic reconnection for the TCP client/server.
*   **Configuration:** Allow TCP host/port to be configured via `default.vrsettings` instead of being hardcoded.
