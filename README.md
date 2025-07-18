# Multicast Experimental Programs (IPv4 & IPv6)

Simple multicast test programs for IPv4 and IPv6, designed for educational use and tested with real Cisco gear.

This repository contains simple yet robust multicast test programs for both IPv4 and IPv6. These tools are designed for educational and experimental use, especially in environments where understanding multicast behavior is critical. The programs have been tested with real Cisco gear and support both ASM and SSM models.

## 🔍 Overview

- `multicast.c`: IPv4 multicast sender/receiver
- `multicast6.c`: IPv6 multicast sender/receiver
- Supports site-local multicast groups (`239.1.1.1`, `ff15::1`)
- Compatible with ASM, SSM and Bidir
- Designed for clarity and practical use in labs and classrooms

## 🧪 Tested With

- Cisco ISR series routers and Catalyst switches 
- VirtualBox environments with USB NICs in bridge mode
- AlmaLinux 9.4 as guest OS on virtual machine
- IGMPv2/v3 and MLDv2 scenarios
- Both IPv4 and IPv6 multicast routing setups

## 🚀 Getting Started

### Build

```bash
make
```

Or manually

```bash
gcc multicast.c -o multicast
gcc multicast6.c -o multicast6
```

### Run

IPv4

```bash
./multicast send 239.1.1.1 12345                          # normal sender
./multicast recv 239.1.1.1 12345                          # normal receiver
./multicast recv 239.1.1.1 12345 172.16.1.1               # SSM receiver
./multicast send 239.1.1.1 12345 - 172.16.1.1             # select local ip to send
./multicast recv 239.1.1.1 12345 - 172.16.2.2             # select local ip to receive
./multicast recv 239.1.1.1 12345 172.16.1.1 172.16.2.2    # SSM & local ip
./multicast both 239.1.1.1 12345                          # bidir sender and receiver
```

IPv6

```bash
./multicast6 send ff15::1 12345                           # normal sender
./multicast6 send ff15::1 12345                           # normal receiver
./multicast6 recv ff15::1 12345 2001:db8:0:1::1           # SSM receiver
./multicast6 send ff15::1 12345 - enp0s3                  # select local i/f to send
./multicast6 recv ff15::1 12345 - enp0s3                  # select local i/f to receive
./multicast6 recv ff15::1 12345 2001:db8:0:1::1 enp0s3    # SSM & local i/f
./multicast6 both ff15::1 12345                           # bidir sender and receiver
```

## 📂 Repository Structure

```
multicast/
├── multicast.c       # IPv4 multicast program
├── multicast6.c      # IPv6 multicast program
├── Makefile          # Build instructions
├── LICENSE           # GNU GPL v3 license
└── README.md         # Project documentation
```

## 📜 License

This project is licensed under the GNU General Public License v3.0. You are free to use, modify, and distribute the code under the same license.

## 👤 Author

Kenji — educator and network engineer  
GitHub: [@laizacruz](https://github.com/laizacruz)  
Email: laizacruz [at] yahoo [dot] co [dot] jp

## 🤝 Contributing

Contributions are welcome! Feel free to fork the repository, submit pull requests, or open issues to improve functionality or documentation.

## 📘 Educational Use

These programs are ideal for:
- Teaching multicast fundamentals
- Demonstrating socket behavior
- Exploring IGMP/MLD interactions
- Validating multicast routing setups
