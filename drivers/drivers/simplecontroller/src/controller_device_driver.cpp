//============ Copyright (c) Valve Corporation, All rights reserved. ============
#include "controller_device_driver.h"

#include "driverlog.h"
// vrmath.h is already included in the header

// Let's create some variables for strings used in getting settings.
static const char* my_controller_main_settings_section = "driver_simplecontroller";
static const char* my_controller_right_settings_section = "driver_simplecontroller_left_controller"; // Naming seems swapped in original
static const char* my_controller_left_settings_section = "driver_simplecontroller_right_controller";
static const char* my_controller_settings_key_model_number = "mycontroller_model_number";
static const char* my_controller_settings_key_serial_number = "mycontroller_serial_number";

#define RECV_BUFFER_SIZE 512

MyControllerDeviceDriver::MyControllerDeviceDriver(vr::ETrackedControllerRole role)
	: my_controller_index_(vr::k_unTrackedDeviceIndexInvalid)
	, my_controller_role_(role)
	, pose_thread_active_(false)
	, tcp_server_active_(false)
	, listen_socket_(INVALID_SOCKET)
	, client_socket_(INVALID_SOCKET)
	, new_imu_data_available_(false)
{
	// Determine port based on role to avoid conflict if two instances are made
	server_port_ = (my_controller_role_ == vr::TrackedControllerRole_LeftHand) ? TCP_PORT_LEFT : TCP_PORT_RIGHT;

	// Initialize IMU data to a sane default
	latest_imu_data_.orientation = HmdQuaternion_Identity;
	latest_imu_data_.a_click = false;
	latest_imu_data_.trigger_click = false;
	latest_imu_data_.trigger_value = 0.0f;

	char model_number[1024];
	vr::VRSettings()->GetString(my_controller_main_settings_section, my_controller_settings_key_model_number, model_number, sizeof(model_number));
	my_controller_model_number_ = model_number;

	char serial_number[1024];
	const char* role_settings_section = (my_controller_role_ == vr::TrackedControllerRole_LeftHand) ? my_controller_left_settings_section : my_controller_right_settings_section;
	vr::VRSettings()->GetString(role_settings_section, my_controller_settings_key_serial_number, serial_number, sizeof(serial_number));
	my_controller_serial_number_ = serial_number;

	DriverLog("My Controller (%s) Model Number: %s", (my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right"), my_controller_model_number_.c_str());
	DriverLog("My Controller (%s) Serial Number: %s", (my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right"), my_controller_serial_number_.c_str());
}

MyControllerDeviceDriver::~MyControllerDeviceDriver()
{
	// Ensure threads are properly shut down if Deactivate wasn't called or didn't complete
	if (tcp_server_active_.exchange(false)) {
		if (listen_socket_ != INVALID_SOCKET) {
#if defined(_WIN32)
			closesocket(listen_socket_);
#else
			close(listen_socket_);
#endif
			listen_socket_ = INVALID_SOCKET;
		}
		if (client_socket_ != INVALID_SOCKET) {
#if defined(_WIN32)
			closesocket(client_socket_);
#else
			close(client_socket_);
#endif
			client_socket_ = INVALID_SOCKET;
		}
		if (my_tcp_server_thread_.joinable()) {
			my_tcp_server_thread_.join();
		}
	}

	if (pose_thread_active_.exchange(false)) {
		if (my_pose_update_thread_.joinable()) {
			my_pose_update_thread_.join();
		}
	}
}


vr::EVRInitError MyControllerDeviceDriver::Activate(uint32_t unObjectId)
{
	my_controller_index_ = unObjectId;

	vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer(my_controller_index_);
	vr::VRProperties()->SetStringProperty(container, vr::Prop_ModelNumber_String, my_controller_model_number_.c_str());
	vr::VRProperties()->SetInt32Property(container, vr::Prop_ControllerRoleHint_Int32, my_controller_role_);
	vr::VRProperties()->SetStringProperty(container, vr::Prop_InputProfilePath_String, "{simplecontroller}/input/mycontroller_profile.json");

	vr::VRDriverInput()->CreateBooleanComponent(container, "/input/a/touch", &input_handles_[MyComponent_a_touch]);
	vr::VRDriverInput()->CreateBooleanComponent(container, "/input/a/click", &input_handles_[MyComponent_a_click]);
	vr::VRDriverInput()->CreateScalarComponent(container, "/input/trigger/value", &input_handles_[MyComponent_trigger_value], vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
	vr::VRDriverInput()->CreateBooleanComponent(container, "/input/trigger/click", &input_handles_[MyComponent_trigger_click]);
	vr::VRDriverInput()->CreateHapticComponent(container, "/output/haptic", &input_handles_[MyComponent_haptic]);

	// Start pose update thread
	pose_thread_active_ = true;
	my_pose_update_thread_ = std::thread(&MyControllerDeviceDriver::MyPoseUpdateThread, this);

	// Start TCP server thread
#if defined(_WIN32)
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		DriverLog("WSAStartup failed: %d", iResult);
		return vr::VRInitError_Driver_Failed;
	}
#endif
	tcp_server_active_ = true;
	new_imu_data_available_ = false;
	latest_imu_data_.orientation = HmdQuaternion_Identity; // Reset
	my_tcp_server_thread_ = std::thread(&MyControllerDeviceDriver::MyTCPServerThreadFunction, this);

	DriverLog("MyControllerDeviceDriver::Activate for %s hand, ObjectId: %d", (my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right"), unObjectId);
	return vr::VRInitError_None;
}

void MyControllerDeviceDriver::MyTCPServerThreadFunction()
{
	DriverLog("TCP Server thread started for %s hand on port %d.", (my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right"), server_port_);

	listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_socket_ == INVALID_SOCKET) {
		DriverLog("Socket creation failed: %d", (_WIN32) ? WSAGetLastError() : errno);
		return;
	}

	sockaddr_in service;
	service.sin_family = AF_INET;
	service.sin_addr.s_addr = inet_addr("0.0.0.0"); // Listen on all available interfaces
	service.sin_port = htons((u_short)server_port_);

	if (bind(listen_socket_, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
		DriverLog("Bind failed: %d", (_WIN32) ? WSAGetLastError() : errno);
#if defined(_WIN32)
		closesocket(listen_socket_);
#else
		close(listen_socket_);
#endif
		listen_socket_ = INVALID_SOCKET;
		return;
	}

	if (listen(listen_socket_, 1) == SOCKET_ERROR) {
		DriverLog("Listen failed: %d", (_WIN32) ? WSAGetLastError() : errno);
#if defined(_WIN32)
		closesocket(listen_socket_);
#else
		close(listen_socket_);
#endif
		listen_socket_ = INVALID_SOCKET;
		return;
	}

	DriverLog("TCP Server listening on port %d for %s hand.", server_port_, (my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right"));

	while (tcp_server_active_)
	{
		if (client_socket_ == INVALID_SOCKET) // Only accept if not already connected
		{
			client_socket_ = accept(listen_socket_, NULL, NULL);
			if (client_socket_ == INVALID_SOCKET) {
				if (tcp_server_active_) { // Avoid error log if we are shutting down
					DriverLog("Accept failed: %d", (_WIN32) ? WSAGetLastError() : errno);
				}
				// If accept fails and we are still active, potentially wait and retry or break
				// For simplicity, if accept fails and we're not shutting down, we might exit the loop or retry after a delay.
				// If shutting down (tcp_server_active_ is false), this failure is expected.
				if (tcp_server_active_) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Don't busy loop on accept errors
					continue;
				}
				else {
					break; // Exit if shutting down
				}
			}
			DriverLog("ESP32 connected to %s hand server.", (my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right"));
		}

		if (client_socket_ != INVALID_SOCKET)
		{
			char recvbuf[RECV_BUFFER_SIZE];
			int recv_len = recv(client_socket_, recvbuf, RECV_BUFFER_SIZE - 1, 0); // -1 for null terminator

			if (recv_len > 0) {
				recvbuf[recv_len] = '\0'; // Null-terminate the received data

				IMUData received_data_temp;
				// Protocol: "qx,qy,qz,qw;btnA_click,btnTrig_click,trig_val\n"
				int items = sscanf_s(recvbuf, "%lf,%lf,%lf,%lf;%d,%d,%f", // Use %lf for float if HmdQuaternion uses double, else %f
					&received_data_temp.orientation.x, &received_data_temp.orientation.y,
					&received_data_temp.orientation.z, &received_data_temp.orientation.w,
					(int*)&received_data_temp.a_click, (int*)&received_data_temp.trigger_click,
					&received_data_temp.trigger_value);

				if (items == 7) { // Check if all parts were parsed
					std::lock_guard<std::mutex> lock(imu_data_mutex_);
					latest_imu_data_ = received_data_temp;
					new_imu_data_available_ = true;
				}
				else {
					DriverLog("Malformed data from ESP32: %s (parsed %d items)", recvbuf, items);
				}

			}
			else if (recv_len == 0) {
				DriverLog("ESP32 disconnected from %s hand server.", (my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right"));
#if defined(_WIN32)
				closesocket(client_socket_);
#else
				close(client_socket_);
#endif
				client_socket_ = INVALID_SOCKET;
			}
			else { // recv_len < 0
				if (tcp_server_active_) {
					DriverLog("Recv failed for %s hand: %d", (my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right"), (_WIN32) ? WSAGetLastError() : errno);
				}
#if defined(_WIN32)
				closesocket(client_socket_);
#else
				close(client_socket_);
#endif
				client_socket_ = INVALID_SOCKET;
				if (!tcp_server_active_) break; // Exit if shutting down
			}
		}
		else // No client connected, and not shutting down
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Prevent busy loop when no client
		}
	}

	if (client_socket_ != INVALID_SOCKET) {
#if defined(_WIN32)
		closesocket(client_socket_);
#else
		close(client_socket_);
#endif
		client_socket_ = INVALID_SOCKET;
	}
	if (listen_socket_ != INVALID_SOCKET) {
#if defined(_WIN32)
		closesocket(listen_socket_);
#else
		close(listen_socket_);
#endif
		listen_socket_ = INVALID_SOCKET;
	}
	DriverLog("TCP Server thread stopped for %s hand.", (my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right"));
}


void* MyControllerDeviceDriver::GetComponent(const char* pchComponentNameAndVersion)
{
	return nullptr;
}

void MyControllerDeviceDriver::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
	if (unResponseBufferSize >= 1)
		pchResponseBuffer[0] = 0;
}

vr::DriverPose_t MyControllerDeviceDriver::GetPose()
{
	vr::DriverPose_t pose = { 0 };
	pose.qWorldFromDriverRotation.w = 1.f; // According to original
	pose.qDriverFromHeadRotation.w = 1.f;  // According to original
	pose.poseIsValid = true;
	pose.deviceIsConnected = true; // Assume connected if ESP32 is sending data or fallback is active
	pose.result = vr::TrackingResult_Running_OK;

	{ // Scope for the lock
		std::lock_guard<std::mutex> lock(imu_data_mutex_);
		if (new_imu_data_available_) {
			pose.qRotation = latest_imu_data_.orientation;
			// If you derive position from IMU (e.g., via sensor fusion), set it here.
			// pose.vecPosition[0] = ...;
			// pose.vecPosition[1] = ...;
			// pose.vecPosition[2] = ...;
		}
		else {
			// Fallback: if no IMU data, use identity rotation or last known good.
			// For this example, let's use identity rotation.
			pose.qRotation = HmdQuaternion_Identity;
		}
	}

	// --- Positional tracking (still HMD-based from simplecontroller) ---
	// You might want to replace this or combine it with IMU-derived position if available.
	vr::TrackedDevicePose_t hmd_pose_arr[1]; // GetRawTrackedDevicePoses needs an array
	vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.f, hmd_pose_arr, 1); // 0.f for "now", 1 device
	vr::TrackedDevicePose_t& hmd_pose = hmd_pose_arr[0];


	if (hmd_pose.bPoseIsValid)
	{
		// Get the position of the hmd from the 3x4 matrix
		const vr::HmdVector3_t hmd_position = HmdVector3_From34Matrix(hmd_pose.mDeviceToAbsoluteTracking);
		// Get the orientation of the hmd (we are overriding controller orientation with IMU, but HMD orientation might be useful for relative positioning)
		const vr::HmdQuaternion_t hmd_orientation = HmdQuaternion_FromMatrix(hmd_pose.mDeviceToAbsoluteTracking);


		// If not using IMU for position, use the HMD-relative logic
		// For this example, let's keep the simplecontroller's HMD-relative positioning logic
		// as a base, and IMU provides orientation.
		// You might want a fixed offset in world space or a more sophisticated setup
		// if your IMU doesn't provide absolute position.

		const vr::HmdVector3_t offset_position = {
			my_controller_role_ == vr::TrackedControllerRole_LeftHand ? -0.15f : 0.15f,
			0.1f,
			-0.3f, // Closer than original simplecontroller for easier viewing
		};

		// Rotate our offset by the hmd quaternion and add the HMD position
		const vr::HmdVector3_t controller_position = hmd_position + (offset_position * hmd_orientation);

		pose.vecPosition[0] = controller_position.v[0];
		pose.vecPosition[1] = controller_position.v[1];
		pose.vecPosition[2] = controller_position.v[2];

		// If your IMU also provides velocity/angular velocity, set them here:
		// pose.vecVelocity = ...
		// pose.vecAngularVelocity = ...
	}
	else
	{
		// HMD pose is not valid, use a default position or mark controller as not fully tracked
		pose.vecPosition[0] = my_controller_role_ == vr::TrackedControllerRole_LeftHand ? -0.15f : 0.15f;
		pose.vecPosition[1] = 1.0f; // Default height
		pose.vecPosition[2] = -0.5f;
		pose.result = vr::TrackingResult_Running_OutOfRange; // Or another appropriate status
	}
	return pose;
}

void MyControllerDeviceDriver::MyPoseUpdateThread()
{
	while (pose_thread_active_) {
		if (my_controller_index_ != vr::k_unTrackedDeviceIndexInvalid) {
			vr::VRServerDriverHost()->TrackedDevicePoseUpdated(my_controller_index_, GetPose(), sizeof(vr::DriverPose_t));
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5)); // ~200Hz
	}
}

void MyControllerDeviceDriver::EnterStandby()
{
	DriverLog("%s hand has been put on standby", my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right");
}

void MyControllerDeviceDriver::Deactivate()
{
	DriverLog("MyControllerDeviceDriver::Deactivate for %s hand, ObjectId: %d", (my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "Left" : "Right"), my_controller_index_.load());

	// Stop TCP server thread
	if (tcp_server_active_.exchange(false)) // Set to false and get previous value
	{
		// Close the listening socket to unblock the accept() call in the server thread
		if (listen_socket_ != INVALID_SOCKET) {
#if defined(_WIN32)
			closesocket(listen_socket_);
#else
			close(listen_socket_);
#endif
			listen_socket_ = INVALID_SOCKET;
		}
		// If a client is connected, closing it might also help the recv() call to unblock/error out
		if (client_socket_ != INVALID_SOCKET) {
#if defined(_WIN32)
			shutdown(client_socket_, SD_BOTH); // Gracefully shut down client connection
			closesocket(client_socket_);
#else
			shutdown(client_socket_, SHUT_RDWR);
			close(client_socket_);
#endif
			client_socket_ = INVALID_SOCKET;
		}

		if (my_tcp_server_thread_.joinable()) {
			my_tcp_server_thread_.join();
		}
	}
#if defined(_WIN32)
	WSACleanup();
#endif

	// Stop pose update thread
	if (pose_thread_active_.exchange(false)) // Set to false and get previous value
	{
		if (my_pose_update_thread_.joinable()) {
			my_pose_update_thread_.join();
		}
	}

	my_controller_index_ = vr::k_unTrackedDeviceIndexInvalid;
}

void MyControllerDeviceDriver::MyRunFrame()
{
	if (my_controller_index_ == vr::k_unTrackedDeviceIndexInvalid)
		return;

	// Update inputs based on latest IMU data
	std::lock_guard<std::mutex> lock(imu_data_mutex_);
	if (new_imu_data_available_) // Could also always update, sending current state
	{
		vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_a_click], latest_imu_data_.a_click, 0.0);
		vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_a_touch], latest_imu_data_.a_click, 0.0); // Assuming click implies touch for simplicity

		vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_trigger_click], latest_imu_data_.trigger_click, 0.0);
		vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_trigger_value], latest_imu_data_.trigger_value, 0.0);

		// new_imu_data_available_ = false; // Uncomment if you only want to process data once
	}
	else
	{
		// Optionally send "false" or "0" if no new data, or maintain last state
		// For simplicity, if no new data, send "off" states
		vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_a_click], false, 0.0);
		vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_a_touch], false, 0.0);
		vr::VRDriverInput()->UpdateBooleanComponent(input_handles_[MyComponent_trigger_click], false, 0.0);
		vr::VRDriverInput()->UpdateScalarComponent(input_handles_[MyComponent_trigger_value], 0.0f, 0.0);
	}
}

void MyControllerDeviceDriver::MyProcessEvent(const vr::VREvent_t& vrevent)
{
	switch (vrevent.eventType) {
	case vr::VREvent_Input_HapticVibration: {
		if (vrevent.data.hapticVibration.componentHandle == input_handles_[MyComponent_haptic]) {
			float duration = vrevent.data.hapticVibration.fDurationSeconds;
			float frequency = vrevent.data.hapticVibration.fFrequency;
			float amplitude = vrevent.data.hapticVibration.fAmplitude;
			DriverLog("Haptic event for %s hand. Duration: %.2f, Freq: %.2f, Amp: %.2f",
				(my_controller_role_ == vr::TrackedControllerRole_LeftHand ? "left" : "right"),
				duration, frequency, amplitude);
			// TODO: If your ESP32 has a vibration motor, send a command back to it here via TCP (if connection is active)
		}
		break;
	}
	default:
		break;
	}
}

const std::string& MyControllerDeviceDriver::MyGetSerialNumber()
{
	return my_controller_serial_number_;
}