# 💬 Simple Chat Server Project

A lightweight **C-based chat server** created for my semester course project.  
The goal of this project is to understand socket programming and basic client–server communication using C.

---

## 🚀 Features
- Multiple clients can connect to the server
- Real-time message broadcasting
- Simple text-based communication
- Command-line interface
- Uses TCP sockets for reliable data transfer

---

## 🧰 Technologies Used
- **C Programming Language**
- **Socket Programming (TCP/IP)**
- **Linux/Windows terminal**

---

## 🏗️ How It Works
1. The server listens for incoming client connections on a specific port.
2. Each connected client can send messages.
3. The server broadcasts messages to all other clients in real-time.

---

## ⚙️ How to Run

### 1. Compile
```bash
gcc server.c -o server
gcc client.c -o client
