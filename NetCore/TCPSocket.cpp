#include "TCPSocket.h"

#include "OverlappedBuffer.h"
#include "Error.h"

#include <Mswsock.h>
#include <iostream>

TCPSocket::TCPSocket(bool overlapped) : Socket(Socket::SOCKET_TYPE::TCP, overlapped) {}

TCPSocket::TCPSocket(SOCKET socket) : Socket(socket) {}

TCPSocket::TCPSocket(TCPSocket&& socket) : Socket(std::move(socket)) {}

TCPSocket& TCPSocket::operator=(TCPSocket&& sock) {
	Socket::operator=(std::move(sock));
	return *this;
}

void TCPSocket::send(const Packet& packet) {
	if (::send(_winSocket, reinterpret_cast<const char*>(packet.getMessageData()), packet.getMessageSize(), 0) == SOCKET_ERROR) {
		int32 errorCode = WSAGetLastError();
		throw errorCode;
	}
}

Packet TCPSocket::receive() {
	uint8* buffer = new uint8[Packet::PACKET_SIZE];

	int32 numBytesreceived = recv(_winSocket, reinterpret_cast<char*>(buffer), Packet::PACKET_SIZE, 0);
	if (numBytesreceived == 0) {
		delete[] buffer;
		// Connection was shutdown
		throw 0;
	}
	else if (numBytesreceived == SOCKET_ERROR) {
		delete[] buffer;
		int32 errorCode = WSAGetLastError();
		throw errorCode;
	}

	Packet packet(buffer, numBytesreceived);

	delete[] buffer;
	return packet;
}

void TCPSocket::listen() {
	if (::listen(_winSocket, SOMAXCONN) == SOCKET_ERROR) {
		int32 errorCode = WSAGetLastError();
		throw errorCode;
	}
}

TCPSocket TCPSocket::accept() {
	SOCKET clientSocket = ::accept(_winSocket, NULL, NULL);
	if (clientSocket == INVALID_SOCKET) {
		int32 errorCode = WSAGetLastError();
		throw errorCode;
	}
	return { clientSocket };
}

TCPSocket TCPSocket::acceptOverlapped(OverlappedBuffer& overlappedBuffer) {
	TCPSocket clientSocket(true);

	uint8* receiveData = new uint8[Packet::PACKET_SIZE];
	DWORD bytesReceived = 0;
	bool result = AcceptEx(
		_winSocket,
		clientSocket._winSocket,
		receiveData,
		0,
		128,
		128,
		&bytesReceived,
		&overlappedBuffer.m_overlapped
	);
	//delete[] receiveData;
	
	if (!result) {
		int32 error = WSAGetLastError();
		if (error != ERROR_IO_PENDING) {
			throw error;
		}
	}

	return clientSocket;
}

void TCPSocket::connect(const IPV4Address& address) {
	if (::connect(_winSocket, address.getSocketAddress(), address.getSocketAddressSize()) == SOCKET_ERROR) {
		int32 errorCode = WSAGetLastError();
		throw errorCode;
	}
}

void TCPSocket::shutdown() {
	if (::shutdown(_winSocket, SD_SEND) == SOCKET_ERROR) {
		int32 errorCode = WSAGetLastError();
		throw errorCode;
	}
}

IPV4Address TCPSocket::getPeerAddress() const {
	sockaddr_in sockAddress;
	int32 sockAddressSize = sizeof(sockaddr_in);
	int32 result = getpeername(_winSocket, reinterpret_cast<sockaddr*>(&sockAddress), &sockAddressSize);
	if (result == SOCKET_ERROR) {
		int32 error = WSAGetLastError();
		throw error;
	}

	return IPV4Address(sockAddress);
}

void TCPSocket::receiveOverlapped(OverlappedBuffer& overlappedBuffer) {
	int32 status = WSARecv(
		_winSocket,
		&overlappedBuffer.m_WSAbuffer,
		1,
		NULL,
		reinterpret_cast<LPDWORD>(&overlappedBuffer.m_flags),
		&overlappedBuffer.m_overlapped,
		NULL
	);
	if (status == SOCKET_ERROR) {
		int32 error = WSAGetLastError();
		if (error != WSA_IO_PENDING) {
			throw error;
		}
	}
}