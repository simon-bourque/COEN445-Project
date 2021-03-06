#include "Log.h"

#include <iostream>
#include <mutex>

static std::mutex g_lock;

void log(LogType logType, MessageType msgType, const IPV4Address& address) {
	std::lock_guard<std::mutex> lock(g_lock);
	
	switch (logType) {
	case LogType::LOG_SEND:
		std::cout << "[Send : ";
		break;
	case LogType::LOG_RECEIVE:
		std::cout << "[Receive : ";
		break;
	}
	
	std::cout << address.getSocketAddressAsString() << "] " << messageTypeToString(msgType) << std::endl;
}

void log(const char* format, ...) {
	std::lock_guard<std::mutex> lock(g_lock);

	va_list args;
	va_start(args, format);

	const int32 numChars = vsnprintf(nullptr, 0, format, args);
	char* buffer = new char[numChars + 1];

	vsprintf_s(buffer, numChars + 1, format, args);

	std::cout << buffer << std::endl;

	delete[] buffer;
	va_end(args);
}