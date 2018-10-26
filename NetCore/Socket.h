#pragma once

#include <Winsock2.h>
#include "Types.h"

class Socket
{
public:
	enum class SOCKET_TYPE
	{
		TCP,
		UDP
	};

	Socket( SOCKET winSocket, addrinfo* addressInfo);
	Socket(const char* address, const char* port, SOCKET_TYPE type);
	Socket(Socket&& sock);
	~Socket();

	void bind();

	virtual void send(uint8* buffer, uint32 bufferSize) = 0;
	virtual void recieve(uint8* buffer, uint32 bufferSize) = 0;

	Socket& operator=(Socket&& sock);
protected:
	SOCKET _winSocket;
	addrinfo* _addressInfo;
};

