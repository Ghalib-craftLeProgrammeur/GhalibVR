import socket
import time
import math

# --- Configuration ---
# Change this to the IP address of the PC running your OpenVR driver
# If running the script on the same PC as the driver, 'localhost' or '127.0.0.1' should work.
# If the ESP32 (and this script, if run on a different machine) is on the same LAN,
# use the PC's local network IP (e.g., 192.168.1.100).
HOST = 'localhost'
# Change this to TCP_PORT_LEFT (12345) or TCP_PORT_RIGHT (12346)
# depending on which controller instance you want to send data to.
PORT = 12345

SEND_INTERVAL_SECONDS = 0.01  # How often to send data (approx 100Hz)
# --- End Configuration ---

def main():
    print(f"Attempting to connect to {HOST}:{PORT}...")
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    try:
        client_socket.connect((HOST, PORT))
        print(f"Connected to {HOST}:{PORT}")
    except ConnectionRefusedError:
        print(f"Connection refused. Is the OpenVR driver running and the TCP server listening on port {PORT}?")
        return
    except Exception as e:
        print(f"Error connecting: {e}")
        return

    # Simulate some changing data
    angle = 0.0
    angle_increment = 0.05  # Radians per send, for orientation change
    trigger_value = 0.0
    trigger_direction = 1 # 1 for increasing, -1 for decreasing
    button_a_state = 0
    trigger_button_state = 0
    loop_count = 0

    try:
        while True:
            # --- Simulate IMU Orientation (simple rotation around Y-axis) ---
            # You would replace this with actual IMU data from your ESP32
            qx = 0.0
            qy = math.sin(angle / 2.0)
            qz = 0.0
            qw = math.cos(angle / 2.0)
            angle += angle_increment

            # --- Simulate Button States and Trigger ---
            loop_count += 1
            if loop_count % 100 == 0: # Toggle button A every 100 sends (approx 1 second)
                button_a_state = 1 - button_a_state
                print(f"Button A state: {button_a_state}")

            trigger_value += 0.02 * trigger_direction
            if trigger_value >= 1.0:
                trigger_value = 1.0
                trigger_direction = -1
                trigger_button_state = 1 # Simulate trigger click at max pull
                print("Trigger FULL, Click ON")
            elif trigger_value <= 0.0:
                trigger_value = 0.0
                trigger_direction = 1
                trigger_button_state = 0 # Release trigger click
                print("Trigger RELEASED, Click OFF")
            elif trigger_value > 0.8 and trigger_button_state == 0: # Hysteresis example for click
                trigger_button_state = 1
                print("Trigger Click ON (near full)")
            elif trigger_value < 0.7 and trigger_button_state == 1: # Hysteresis example for click
                trigger_button_state = 0
                print("Trigger Click OFF (releasing)")


            # --- Format the data string ---
            # qx,qy,qz,qw;btnA_click,btnTrig_click,trig_val\n
            data_string = f"{qx:.4f},{qy:.4f},{qz:.4f},{qw:.4f};{button_a_state},{trigger_button_state},{trigger_value:.2f}\n"

            try:
                # Send data
                client_socket.sendall(data_string.encode('utf-8'))
                # print(f"Sent: {data_string.strip()}") # Uncomment for verbose output
            except socket.error as e:
                print(f"Socket error sending data: {e}")
                break # Exit loop on send error
            except Exception as e:
                print(f"Error sending data: {e}")
                break


            time.sleep(SEND_INTERVAL_SECONDS)

    except KeyboardInterrupt:
        print("Closing connection...")
    finally:
        client_socket.close()
        print("Connection closed.")

if __name__ == "__main__":
    main()