#include "TCPSocket.h"

TCPSocket::TCPSocket() : Socket(Socket::SOCKET_TYPE::TCP) {}

TCPSocket::TCPSocket(SOCKET socket) : Socket(socket) {}

void TCPSocket::send(const Packet& packet) {
	if (::send(_winSocket, reinterpret_cast<const char*>(packet.getMessageData()), packet.getMessageSize(), 0) == SOCKET_ERROR) {
		int32 errorCode = WSAGetLastError();
		throw std::runtime_error("Failed to send packet: " + WSAErrorCodeToString(errorCode));

	}
}

Packet TCPSocket::receive() {
	int errorCode;
	uint8* buffer = new uint8[Packet::PACKET_SIZE];

	errorCode = recv(_winSocket, reinterpret_cast<char*>(buffer), Packet::PACKET_SIZE, 0);
	if (errorCode > 0)
		throw std::runtime_error("Received bytes:" + errorCode);
	else if (errorCode == 0)
		throw std::runtime_error("Connection was closed");
	else
		throw std::runtime_error("Received has failed: " + WSAErrorCodeToString(errorCode));

	return Packet(buffer, Packet::PACKET_SIZE);
}

void TCPSocket::listen() {
	if (::listen(_winSocket, SOMAXCONN) == SOCKET_ERROR) {
		int32 errorCode = WSAGetLastError();
		throw std::runtime_error("Failed to listen to socket: " + WSAErrorCodeToString(errorCode));
	}
}

TCPSocket TCPSocket::accept() {
	SOCKET clientSocket = ::accept(_winSocket, NULL, NULL);
	if (clientSocket == INVALID_SOCKET) {
		int32 errorCode = WSAGetLastError();
		throw std::runtime_error("Failed to accept socket: " + WSAErrorCodeToString(errorCode));
	}
	return { clientSocket };
}

void TCPSocket::connect(const IPV4Address& address) {
	if (::connect(_winSocket, address.getSocketAddress(), address.getSocketAddressSize()) == SOCKET_ERROR) {
		int32 errorCode = WSAGetLastError();
		throw std::runtime_error("Failed to connect to socket: " + WSAErrorCodeToString(errorCode));
	}
}

void TCPSocket::shutdown() {
	if (::shutdown(_winSocket, SD_SEND) == SOCKET_ERROR) {
		int32 errorCode = WSAGetLastError();
		throw std::runtime_error("Failed to shutdown socket: " + WSAErrorCodeToString(errorCode));
	}
}