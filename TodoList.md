TCP Reader / TCP Reader App Options

You have two implementation options:
Option 1: TCP Reader App

    Build a separate application (TCP Reader App).

    This app will handle TCP packet reading.

    Communicate with your main application to transfer the interpreted controller data.

Option 2: TCP Reader in C++

    Implement the TCP Reader directly in the C++ code.

    No separate app is needed.

    Directly translate the packet data into controller input using OpenVR APIs.
