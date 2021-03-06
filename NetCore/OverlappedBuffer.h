#pragma once

#include "Types.h"
#include "Packet.h"
#include "IPV4Address.h"

#include <Winsock2.h>

constexpr uint32 OVERLAPPED_BUFFER_SIZE = 512;

class Connection;

class OverlappedBuffer {
	friend class UDPSocket;
	friend class TCPSocket;
	friend class Connection;
private:
	char* m_buffer;
	WSABUF m_WSAbuffer;
	WSAOVERLAPPED m_overlapped;

	uint32 m_flags;

	sockaddr_in m_senderAddress;
	int32 m_senderAddressSize;
public:
	OverlappedBuffer();
	virtual ~OverlappedBuffer();

	uint8* getData();
	IPV4Address getAddress() { return IPV4Address(m_senderAddress); }
};

