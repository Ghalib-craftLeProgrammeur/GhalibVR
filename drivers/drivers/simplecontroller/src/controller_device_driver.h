//============ Copyright (c) Valve Corporation, All rights reserved. ============
#pragma once

#include <array>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector> // For recv buffer if needed, though char array is fine

#include "openvr_driver.h"
#include "vrmath.h" // For HmdQuaternion_t, HmdVector3_t, etc.

// Networking includes (Windows)
#if defined(_WIN32)
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
// Linux/macOS socket includes
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> // for close
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
typedef int SOCKET;
#endif

// Define ports for left and right controllers
#define TCP_PORT_LEFT 12345
#define TCP_PORT_RIGHT 12346

enum MyComponent
{
	MyComponent_a_touch,
	MyComponent_a_click,

	MyComponent_trigger_value,
	MyComponent_trigger_click,

	MyComponent_haptic,

	MyComponent_MAX
};

struct IMUData {
	vr::HmdQuaternion_t orientation;
	float trigger_value;
	bool a_click;
	bool trigger_click;
	// Add other data like button presses, joystick axes, etc.
	// For simplicity, we'll just add a button state
	// uint64_t timestamp; // Optional, for freshness
};


//-----------------------------------------------------------------------------
// Purpose: Represents a single tracked device in the system.
// What this device actually is (controller, hmd) depends on the
// properties you set within the device (see implementation of Activate)
//-----------------------------------------------------------------------------
class MyControllerDeviceDriver : public vr::ITrackedDeviceServerDriver
{
public:
	MyControllerDeviceDriver( vr::ETrackedControllerRole role );
	~MyControllerDeviceDriver(); // Destructor to manage thread

	vr::EVRInitError Activate( uint32_t unObjectId ) override;

	void EnterStandby() override;

	void *GetComponent( const char *pchComponentNameAndVersion ) override;

	void DebugRequest( const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize ) override;

	vr::DriverPose_t GetPose() override;

	void Deactivate() override;

	// ----- Functions we declare ourselves below -----

	const std::string &MyGetSerialNumber();

	void MyRunFrame();
	void MyProcessEvent( const vr::VREvent_t &vrevent );

	void MyPoseUpdateThread();

private:
	void MyTCPServerThreadFunction(); // The function our TCP server thread will run

	std::atomic< vr::TrackedDeviceIndex_t > my_controller_index_;
	vr::ETrackedControllerRole my_controller_role_;

	std::string my_controller_model_number_;
	std::string my_controller_serial_number_;

	std::array< vr::VRInputComponentHandle_t, MyComponent_MAX > input_handles_;

	// Pose update thread
	std::atomic< bool > pose_thread_active_;
	std::thread my_pose_update_thread_;

	// TCP Server members
	std::thread my_tcp_server_thread_;
	std::atomic<bool> tcp_server_active_;
	SOCKET listen_socket_;
	SOCKET client_socket_;
	int server_port_;

	// Shared IMU data
	std::mutex imu_data_mutex_;
	IMUData latest_imu_data_;
	bool new_imu_data_available_; // To indicate if GetPose should use IMU data
};