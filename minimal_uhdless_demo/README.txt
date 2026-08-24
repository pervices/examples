This is a demo showcasing the minimum viable program to stream without UHD.
The APIs used between UHD and the device are substantially less stable than the UHD API.
This example is provided for people who must stream without UHD; it is strongly recommended against.

This example is based on UHD PV commit 4158287a89ecc7765886d0c8d64448cbe0ac9467.

Compilation instructions:
Replace 192.168.10.2 (default management port's IP) and 10.10.10.2 (default SFP A) with your new management port/SFP A IP if you changed them from the default.
Run "build.sh" and follow the prompts. On newer systems it should just work.
On old systems such as RHEL 8, you may need to set up gcc to work with C++20.

Runtime instructions:
Run "build/send_main". It will start a 10s stream 10s into the future.
