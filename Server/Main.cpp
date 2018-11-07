#include <iostream>
#include <unordered_map>

#include "UDPSocket.h"
#include "TCPSocket.h"
#include "ThreadPool.h"
#include "WSA.h"
#include "Connection.h"
#include "Messages.h"

std::unordered_map<std::string, Connection> g_connections;

int main() {
	std::cout << "Initializing server..." << std::endl;

	ThreadPool::init();
	WSA::init();

	// Create listen thread
	ThreadPool::get()->submit([](PTP_CALLBACK_INSTANCE instance, PVOID parameter, PTP_WORK work) {
		UNREFERENCED_PARAMETER(work);

		CallbackMayRunLong(instance);

		// Create listen socket
		UDPSocket listenerSocket;

		IPV4Address address("127.0.0.1", DEFAULT_PORT);
		listenerSocket.bind(address);

		std::cout << "Started listening..." << std::endl;
		bool listening = true;
		while (listening) {
			Packet packet = listenerSocket.receive();
			MessageType type = static_cast<MessageType>(packet.getMessageData()[0]);
			std::cout << "[Receive] " << messageTypeToString(type) << " message" << std::endl;
			std::cout << packet.getAddress().getSocketAddressAsString() << std::endl;
			std::cout << packet.getAddress().getSocketPortAsString() << std::endl;

			switch (type) {
			case MessageType::MSG_REGISTER:
				RegisterMessage msg = deserializeMessage<RegisterMessage>(packet);

				// REGISTER HIM!
				g_connections[msg.name] = Connection();

				break;
			}
		}
	}, nullptr);


	// Using main thread as listen thread
	TCPSocket listenerSocket;
	IPV4Address address("127.0.0.1", DEFAULT_PORT);
	listenerSocket.bind(address);

	std::cout << "Started listening..." << std::endl;
	listenerSocket.listen();

	bool listening = true;
	while (listening) {
		TCPSocket clientSocket = listenerSocket.accept();
		std::cout << "Accepted connection..." << std::endl;

		ThreadPool::get()->submit([](PTP_CALLBACK_INSTANCE instance, PVOID parameter, PTP_WORK work) {
			UNREFERENCED_PARAMETER(work);

			CallbackMayRunLong(instance);

			// Receive data from connection until client shuts down connection
			// https://docs.microsoft.com/en-us/windows/desktop/winsock/receiving-and-sending-data-on-the-server
			
		}, nullptr);
	}

	ThreadPool::get()->clean();
	ThreadPool::destroy();
	WSA::destroy();

	return 0;
}