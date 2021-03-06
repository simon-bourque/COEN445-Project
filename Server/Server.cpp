#include "Server.h"

#include "Log.h"
#include "UDPSocket.h"
#include "TCPSocket.h"
#include "Error.h"
#include "Item.h"

#include <Mswsock.h>
#include <iostream>
#include <fstream>
#include <mutex>

void udpServiceRoutine(PTP_CALLBACK_INSTANCE instance, PVOID parameter, PTP_WORK work);
void tcpServiceRoutine(PTP_CALLBACK_INSTANCE instance, PVOID parameter, PTP_WORK work);
void connectionServiceRoutine(PTP_CALLBACK_INSTANCE instance, PVOID parameter, PTP_WORK work);

std::mutex g_auctionLock;

Server::Server(const IPV4Address& bindAddress) : 
	m_serverBindAddress(bindAddress)
	, m_serverUDPSocket(true)
	, m_serverTCPSocket(true)
	, m_running(true)
{
	m_udpServiceIOPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	m_tcpServiceIOPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	m_connectionServiceIOPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
}

Server::~Server() {}

void Server::shutdown() {
	m_running = false;
	m_serverUDPSocket.close();
	m_serverTCPSocket.close();
	saveConnections();
	m_connections.clear();

	PostQueuedCompletionStatus(m_udpServiceIOPort, 0, 0, nullptr);
	PostQueuedCompletionStatus(m_tcpServiceIOPort, 0, 0, nullptr);
	PostQueuedCompletionStatus(m_connectionServiceIOPort, 0, 0, nullptr);
}

void Server::startUDPServiceThread() {
	m_serverUDPSocket.bind(m_serverBindAddress);
	
	CreateIoCompletionPort(m_serverUDPSocket.getWinSockHandle(), m_udpServiceIOPort, 1, 0);

	m_serverUDPSocket.receiveOverlapped(m_serverUDPBuffer);
	ThreadPool::get()->submit(udpServiceRoutine, this);
}

void Server::startTCPServiceThread() {
	m_serverTCPSocket.bind(m_serverBindAddress);
	m_serverTCPSocket.listen();

	CreateIoCompletionPort(m_serverTCPSocket.getWinSockHandle(), m_tcpServiceIOPort, 1, 0);
	
	ThreadPool::get()->submit(tcpServiceRoutine, this);
}

void Server::startConnectionServiceThread() {
	ThreadPool::get()->submit(connectionServiceRoutine, this);
}

void Server::sendRegistered(uint32 reqNum, const std::string& name, const std::string& ip, const std::string& port, const IPV4Address& address) {
	RegisteredMessage registeredMsg;
	registeredMsg.reqNum = reqNum;
	memcpy(registeredMsg.name, name.c_str(), name.size());
	memcpy(registeredMsg.iPAddress, ip.c_str(), ip.size());
	memcpy(registeredMsg.port, port.c_str(), port.size());

	Packet registeredPacket = serializeMessage(registeredMsg);
	registeredPacket.setAddress(address);

	m_serverUDPSocket.send(registeredPacket);
	log(LogType::LOG_SEND, registeredMsg.type, registeredPacket.getAddress());
}

void Server::sendUnregistered(uint32 reqNum, const std::string& reason, const IPV4Address& address) {
	UnregisteredMessage unregisteredMsg;
	unregisteredMsg.reqNum = reqNum;
	memcpy(unregisteredMsg.reason, reason.c_str(), reason.size() + 1);

	Packet packet = serializeMessage(unregisteredMsg);
	packet.setAddress(address);

	m_serverUDPSocket.send(packet);
	log(LogType::LOG_SEND, unregisteredMsg.type, packet.getAddress());
}

void Server::sendDeregConf(uint32 reqNum, const IPV4Address& address) {
	DeregConfMessage deregConfMsg;
	deregConfMsg.reqNum = reqNum;

	Packet deregConfPacket = serializeMessage(deregConfMsg);
	deregConfPacket.setAddress(address);

	m_serverUDPSocket.send(deregConfPacket);
	log(LogType::LOG_SEND, deregConfMsg.type, deregConfPacket.getAddress());
}

void Server::sendDeregDenied(uint32 reqNum, const std::string& reason, const IPV4Address& address) {
	DeregDeniedMessage deregDeniedMsg;
	deregDeniedMsg.reqNum = reqNum;
	memcpy(deregDeniedMsg.reason, reason.c_str(), reason.size() + 1);

	Packet deregDeniedPacket = serializeMessage(deregDeniedMsg);
	deregDeniedPacket.setAddress(address);

	m_serverUDPSocket.send(deregDeniedPacket);
	log(LogType::LOG_SEND, deregDeniedMsg.type, deregDeniedPacket.getAddress());
}

void Server::sendOfferConf(uint32 reqNum, uint32 itemNum, const std::string& description, float32 minimum, const IPV4Address& address) {
	OfferConfMessage offerConfMsg;
	offerConfMsg.reqNum = reqNum;
	memcpy(offerConfMsg.description, description.c_str(), description.size() + 1);
	offerConfMsg.minimum = minimum;
	offerConfMsg.itemNum = itemNum;

	Packet offerConfPacket = serializeMessage(offerConfMsg);
	offerConfPacket.setAddress(address);

	m_serverUDPSocket.send(offerConfPacket);
	log(LogType::LOG_SEND, offerConfMsg.type, offerConfPacket.getAddress());
}

void Server::sendOfferDenied(uint32 reqNum, const std::string& reason, const IPV4Address& address) {
	OfferDeniedMessage offerDeniedMsg;
	offerDeniedMsg.reqNum = reqNum;
	memcpy(offerDeniedMsg.reason, reason.c_str(), reason.size() + 1);

	Packet offerDeniedPacket = serializeMessage(offerDeniedMsg);
	offerDeniedPacket.setAddress(address);

	m_serverUDPSocket.send(offerDeniedPacket);
	log(LogType::LOG_SEND, offerDeniedMsg.type, offerDeniedPacket.getAddress());
}

void Server::sendNewItem(const Item& item) {
	NewItemMessage newItemMsg;
	newItemMsg.itemNum = item.getItemID();
	memcpy(newItemMsg.description, item.getDescription().c_str(), item.getDescription().size() + 1);
	newItemMsg.minimum = item.getMinimum();
	newItemMsg.port[0] = '\0';

	Packet packet = serializeMessage(newItemMsg);

	// Send to everyone registered
	for (auto& pair : m_connections) {
		Connection& connection = pair.second;
		if (connection.isConnected()) {
			packet.setAddress(connection.getAddress());
			
			m_serverUDPSocket.send(packet);
			log(LogType::LOG_SEND, newItemMsg.type, packet.getAddress());
		}
	}
}

void Server::sendHighest(const Item& item) {
	HighestMessage highMsg;
	highMsg.itemNum = item.getItemID();
	highMsg.amount = item.getCurrentHighest();
	memcpy(highMsg.description, item.getDescription().c_str(), item.getDescription().size() + 1);

	Packet packet = serializeMessage(highMsg);

	// Send to everyone registered
	for (auto& pair : m_connections) {
		Connection& connection = pair.second;
		if (connection.isConnected()) {
			connection.send(packet);
			log(LogType::LOG_SEND, highMsg.type, connection.getAddress());
		}
	}
}

void Server::sendWin(const Item& item) {
	WinMessage winMsg;
	winMsg.itemNum = item.getItemID();
	winMsg.amount = item.getCurrentHighest();
	winMsg.port[0] = '\0';

	// Get seller connection
	auto iter = m_connections.find(item.getSeller());
	if (iter != m_connections.end()) {
		Connection& connection = iter->second;
		memcpy(winMsg.name, connection.getUniqueName().c_str(), connection.getUniqueName().size() + 1);
		memcpy(winMsg.iPAddress, connection.getAddress().getSocketAddressAsString().c_str(), connection.getAddress().getSocketAddressAsString().size() + 1);
	}
	else {
		winMsg.name[0] = '\0';
		winMsg.iPAddress[0] = '\0';
	}

	auto iter2 = m_connections.find(item.getHighestBidder());
	if (iter2 != m_connections.end()) {
		Connection& winner = iter2->second;
		if (winner.isConnected()) {
			Packet packet = serializeMessage(winMsg);
			winner.send(packet);
			log(LogType::LOG_SEND, winMsg.type, winner.getAddress());
		}
	}
}

void Server::sendBidOver(const Item& item) {
	BidOverMessage bidOverMsg;
	bidOverMsg.itemNum = item.getItemID();
	bidOverMsg.amount = item.getCurrentHighest();

	Packet packet = serializeMessage(bidOverMsg);

	// Send to everyone registered
	for (auto& pair : m_connections) {
		Connection& connection = pair.second;
		if (connection.isConnected()) {
			connection.send(packet);
			log(LogType::LOG_SEND, bidOverMsg.type, connection.getAddress());
		}
	}
}

void Server::sendSoldTo(const Item& item) {
	SoldToMessage soldToMsg;
	soldToMsg.itemNum = item.getItemID();
	soldToMsg.amount = item.getCurrentHighest();
	soldToMsg.port[0] = '\0';

	// Get winner connection
	auto iter = m_connections.find(item.getHighestBidder());
	if (iter != m_connections.end()) {
		Connection& connection = iter->second;
		memcpy(soldToMsg.name, connection.getUniqueName().c_str(), connection.getUniqueName().size() + 1);
		memcpy(soldToMsg.iPAddress, connection.getAddress().getSocketAddressAsString().c_str(), connection.getAddress().getSocketAddressAsString().size() + 1);
	}
	else {
		soldToMsg.name[0] = '\0';
		soldToMsg.iPAddress[0] = '\0';
	}

	auto iter2 = m_connections.find(item.getSeller());
	if (iter2 != m_connections.end()) {
		Connection& seller = iter2->second;
		if (seller.isConnected()) {
			Packet packet = serializeMessage(soldToMsg);
			seller.send(packet);
			log(LogType::LOG_SEND, soldToMsg.type, seller.getAddress());
		}
	}
}

void Server::sendNotSold(const Item& item) {
	NotSoldMessage notSoldMsg;
	notSoldMsg.itemNum = item.getItemID();
	memcpy(notSoldMsg.reason, "No valid bids", 14);

	// Find seller
	auto iter = m_connections.find(item.getSeller());
	if (iter != m_connections.end()) {
		Connection& seller = iter->second;
		if (seller.isConnected()) {
			Packet packet = serializeMessage(notSoldMsg);
			seller.send(packet);
			log(LogType::LOG_SEND, notSoldMsg.type, seller.getAddress());
		}
	}
}

void Server::startAuction(const Item& item, uint64 auctionTime) {
	std::lock_guard<std::mutex> lock(g_auctionLock);

	Item* newItem = new Item(item);
	m_offeredItems[item.getItemID()] = newItem;

	log("[INFO] Starting auction for item number %u with a min bid of %.2f", item.getItemID(), item.getMinimum());
	sendNewItem(item);

	std::pair<Server*, Item*>* pair = new std::pair<Server*, Item*>(this, newItem);

	FILETIME fileTime;
	GetSystemTimeAsFileTime(&fileTime);
	ULARGE_INTEGER* time = reinterpret_cast<ULARGE_INTEGER*>(&fileTime);
	newItem->setAuctionStartTime(time->QuadPart);

	ThreadPool::get()->submitTimer([](PTP_CALLBACK_INSTANCE instance, PVOID arg, PTP_TIMER timer) {
		std::pair<Server*, Item*>* inPair = reinterpret_cast<std::pair<Server*, Item*>*>(arg);
		Server* server = inPair->first;
		Item* finishedItem = inPair->second;

		inPair->first->endAuction(*finishedItem);
		log("[INFO] Auction ended for item number %u with a price of %.2f", finishedItem->getItemID(), finishedItem->getCurrentHighest());

		delete finishedItem;
		delete inPair;
		CloseThreadpoolTimer(timer);
	}, pair, auctionTime);

	saveConnections();
}

void Server::bid(uint32 itemID, float32 newBid, const std::string& bidder) {
	std::lock_guard<std::mutex> lock(g_auctionLock);

	auto iter = m_offeredItems.find(itemID);
	if (iter != m_offeredItems.end()) {
		Item* item = iter->second;
		if (newBid > item->getCurrentHighest()) {
			if (item->getSeller() != bidder) {
				item->setCurrentHighest(newBid);
				item->setHighestBidder(bidder);
				sendHighest(*item);
			}
			else {
				log("[INFO] Client attempting to bid on own item %u, ignoring bid", itemID);
			}
		}
		else {
			log("[INFO] New bid of %.2f below current bid for item %u, ignoring bid", newBid, itemID);
		}
	}
	else {
		log("[INFO] Item %u not up for auction, ignoring bid", itemID);
	}
}

void Server::endAuction(const Item& item) {
	std::lock_guard<std::mutex> lock(g_auctionLock);

	m_offeredItems.erase(item.getItemID());
	saveConnections();

	// SEND TCP PACKETS
	sendBidOver(item);

	// Check if anyone bid on the item
	if (item.getCurrentHighest() != item.getMinimum()) {
		sendWin(item);
		sendSoldTo(item);
	}
	else {
		sendNotSold(item);
	}
}

bool Server::isSeller(const std::string& seller) {
	std::lock_guard<std::mutex> lock(g_auctionLock);

	for (auto& pair : m_offeredItems) {
		Item* item = pair.second;
		if (item->getSeller() == seller) {
			return true;
		}
	}

	return false;
}

bool Server::isHighestBidder(const std::string& bidder) {
	std::lock_guard<std::mutex> lock(g_auctionLock);

	for (auto& pair : m_offeredItems) {
		Item* item = pair.second;
		if (item->getHighestBidder() == bidder) {
			return true;
		}
	}

	return false;
}

int32 Server::getNumOffers(const std::string& seller) {
	std::lock_guard<std::mutex> lock(g_auctionLock);

	int32 count = 0;
	for (auto& pair : m_offeredItems) {
		Item* item = pair.second;
		if (item->getSeller() == seller) {
			count++;
		}
	}

	return count;
}

void Server::handlePacket(const Packet& packet) {
	MessageType type = static_cast<MessageType>(packet.getMessageData()[0]);
	log(LogType::LOG_RECEIVE, type, packet.getAddress());

	switch (type) {
	case MessageType::MSG_REGISTER:
		handleRegisterPacket(packet);
		break;
	case MessageType::MSG_DEREGISTER:
		handleDeregisterPacket(packet);
		break;
	case MessageType::MSG_OFFER:
		handleOfferPacket(packet);
		break;
	case MessageType::MSG_BID:
		handleBidPacket(packet);
		break;
	}

}

void Server::handleRegisterPacket(const Packet& packet) {
	RegisterMessage msg = deserializeMessage<RegisterMessage>(packet);

	std::string name(msg.name);
	// Check if same name
	for (auto& pair : m_connections) {
		Connection& connection = pair.second;
		if (name == connection.getUniqueName() && connection.getAddress().getSocketAddressAsString() != packet.getAddress().getSocketAddressAsString()) {
			sendUnregistered(msg.reqNum, "Name already exists", packet.getAddress());
			return;
		}
	}

	// Attempt to register
	auto iter = m_connections.find(packet.getAddress().getSocketAddressAsString());
	if (iter == m_connections.end()) {
		log("[INFO] Registering client %s (%s)", msg.name, packet.getAddress().getSocketAddressAsString().c_str());
		m_connections[packet.getAddress().getSocketAddressAsString()] = Connection(std::string(msg.name), packet.getAddress());
	}
	else {
		log("[INFO] Client %s (%s) already registered", msg.name, packet.getAddress().getSocketAddressAsString().c_str());
		iter->second.setUniqueName(msg.name);
		iter->second.setAddress(packet.getAddress());
	}
	saveConnections();

	sendRegistered(msg.reqNum, std::string(msg.name), std::string(msg.iPAddress), std::string(msg.port), packet.getAddress());
}

void Server::handleDeregisterPacket(const Packet& packet) {
	DeregisterMessage msg = deserializeMessage<DeregisterMessage>(packet);

	// DEREGISTER HIM!
	auto it = m_connections.find(packet.getAddress().getSocketAddressAsString());
	if (it != m_connections.end())
	{
		if (isSeller(it->second.getAddress().getSocketAddressAsString())) {
			sendDeregDenied(msg.reqNum, "Pending offer", packet.getAddress());
			return;
		}
		if (isHighestBidder(it->second.getAddress().getSocketAddressAsString())) {
			sendDeregDenied(msg.reqNum, "Highest bidder", packet.getAddress());
			return;
		}

		// User was found in the registered table, remove him
		sendDeregConf(msg.reqNum, packet.getAddress());

		(*it).second.shutdown();
		m_connections.erase(it);

		saveConnections();
	}
	else
	{
		// User was not found in the registered table
		sendDeregDenied(msg.reqNum, "User not registered", packet.getAddress());
	}
}

void Server::handleOfferPacket(const Packet& packet) {
	OfferMessage msg = deserializeMessage<OfferMessage>(packet);
	
	auto iter = m_connections.find(packet.getAddress().getSocketAddressAsString());
	const bool connected = (iter != m_connections.end()) ? (*iter).second.isConnected() : false;

	if (connected) {
		Connection& connection = (*iter).second;

		if (getNumOffers(packet.getAddress().getSocketAddressAsString()) >= 3) {
			sendOfferDenied(msg.reqNum, "Too many offers (max 3)", packet.getAddress());
			return;
		}

		if (msg.reqNum > connection.getOfferReqNumber()) {
			// Client offering new item
			Item item(std::string(msg.description), msg.minimum, connection.getAddress().getSocketAddressAsString());

			connection.setLastItemOfferedID(item.getItemID());
			connection.setOfferReqNumber(msg.reqNum);

			// Send confirmation
			sendOfferConf(msg.reqNum, item.getItemID(), std::string(msg.description), msg.minimum, packet.getAddress());

			startAuction(item);
		}
		else {
			// Client resend same item
			auto iter = m_offeredItems.find(connection.getLastItemOfferedID());
			if (iter != m_offeredItems.end()) {
				// Send confirmation
				sendOfferConf(msg.reqNum, connection.getLastItemOfferedID(), std::string(msg.description), msg.minimum, packet.getAddress());
			}
			else {
				// REALLY REALLY BAD I hope this never happens
				sendOfferDenied(msg.reqNum, "Invalid request number", packet.getAddress());
			}
		}
	}
	else {
		// Client not registered
		sendOfferDenied(msg.reqNum, "User not registered", packet.getAddress());
	}
}

void Server::handleBidPacket(const Packet& packet) {
	BidMessage bidMsg = deserializeMessage<BidMessage>(packet);
	bid(bidMsg.itemNum, bidMsg.amount, packet.getAddress().getSocketAddressAsString());
}

void udpServiceRoutine(PTP_CALLBACK_INSTANCE instance, PVOID parameter, PTP_WORK work) {
	UNREFERENCED_PARAMETER(work);
	CallbackMayRunLong(instance);

	Server* server = reinterpret_cast<Server*>(parameter);

	DWORD numBytes = 0;
	ULONG_PTR key = 0;
	LPOVERLAPPED overlapped = nullptr;

	log("[INFO] Started listening on UDP port %s", server->m_serverBindAddress.getSocketPortAsString().c_str());
	while (server->m_running) {
		bool status = GetQueuedCompletionStatus(server->m_udpServiceIOPort, &numBytes, &key, &overlapped, INFINITE);
		if (!status) {
			// Most likely shutting down
			break;
		}
		if (key == 0) {
			// Shutdown requested
			break;
		}

		// Convert OverlappedBuffer to Packet for ease of use
		OverlappedBuffer& buffer = server->m_serverUDPBuffer;
		Packet packet(buffer.getData(), numBytes);
		packet.setAddress(buffer.getAddress());

		server->handlePacket(packet);

		try {
			server->m_serverUDPSocket.receiveOverlapped(buffer);
		}
		catch (int32 error) {
			log("[ERROR] %s", getWSAErrorString(error).c_str());
			break;
		}
	}
	log("[INFO] UDP service routine shutdown");
}

void tcpServiceRoutine(PTP_CALLBACK_INSTANCE instance, PVOID parameter, PTP_WORK work) {
	UNREFERENCED_PARAMETER(work);
	CallbackMayRunLong(instance);

	Server* server = reinterpret_cast<Server*>(parameter);

	DWORD numBytes = 0;
	ULONG_PTR key = 0;
	LPOVERLAPPED overlapped = nullptr;
	TCPSocket acceptedSocket = server->m_serverTCPSocket.acceptOverlapped(server->m_serverTCPBuffer);

	log("[INFO] Started listening on TCP port %s", server->m_serverBindAddress.getSocketPortAsString().c_str());
	while (server->m_running) {
		bool result = GetQueuedCompletionStatus(server->m_tcpServiceIOPort, &numBytes, &key, &overlapped, INFINITE);
		if (!result) {
			DWORD error = GetLastError();

			if (error == ERROR_ABANDONED_WAIT_0) {
				break;
			}
			if (error == ERROR_OPERATION_ABORTED) {
				// shutdown
				break;
			}

			std::cout << "[ERROR] " << getWindowsErrorString(error);
			break;
		}
		if (key == 0) {
			// shutdown
			break;
		}
		//std::cout << "Accepted connection..." << std::endl;

		SOCKET socketHandle = server->m_serverTCPSocket.getWinSockSocket();
		int32 resultOpt = setsockopt(acceptedSocket.getWinSockSocket(), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char*>(&socketHandle), sizeof(socketHandle));
		if (resultOpt == SOCKET_ERROR) {
			int32 error = WSAGetLastError();
			std::cout << getWSAErrorString(error) << std::endl;
		}

		IPV4Address peerAddress = acceptedSocket.getPeerAddress();
		//std::cout << peerAddress.getSocketAddressAsString() << std::endl;
		auto connectionIter = server->m_connections.find(peerAddress.getSocketAddressAsString());
		if (connectionIter != server->m_connections.end()) {
			connectionIter->second.connect(std::move(acceptedSocket), server->m_connectionServiceIOPort);
		}

		acceptedSocket = server->m_serverTCPSocket.acceptOverlapped(server->m_serverTCPBuffer);
	}

	log("[INFO] TCP service routine shutdown");
}

void connectionServiceRoutine(PTP_CALLBACK_INSTANCE instance, PVOID parameter, PTP_WORK work) {
	UNREFERENCED_PARAMETER(work);
	CallbackMayRunLong(instance);

	Server* server = reinterpret_cast<Server*>(parameter);
	
	DWORD numBytes = 0;
	ULONG_PTR key = 0;
	LPOVERLAPPED overlapped = nullptr;

	while (server->m_running) {
		bool result = GetQueuedCompletionStatus(server->m_connectionServiceIOPort, &numBytes, &key, &overlapped, INFINITE);
		Connection* connection = reinterpret_cast<Connection*>(key);
		
		if (!result) {
			DWORD error = GetLastError();

			if (error == ERROR_ABANDONED_WAIT_0) {
				continue;
			}
			if (error == ERROR_OPERATION_ABORTED) {
				// Connection shutdown by server
				continue;
			}
			if (error == ERROR_NETNAME_DELETED) {
				// Ungraceful shutdown (AKA crash on client)
				// TODO delete connection data if not bidding
				connection->shutdown();
				continue;
			}

			std::cout << "[ERROR] " << getWindowsErrorString(error);
			continue;
		}
		if (key == 0) {
			// Server shutdown
			break;
		}
		if (numBytes == 0) {
			// Connection shutdown by client
			// TODO delete connection data if not bidding
			connection->shutdown();
			continue;
		}


		OverlappedBuffer& buffer = connection->getOverlappedBuffer();
		Packet packet(buffer.getData(), numBytes);
		packet.setAddress(connection->getAddress());

		// Handle packet
		server->handlePacket(packet);

		connection->receiveOverlapped();
	}
	log("[INFO] Connection service routine shutdown");
}

void Server::saveConnections() {
	std::ofstream output("connections.dat");

	if (!output) {
		log("[ERROR] Failed to save connections to file");
		return;
	}

	output << m_connections.size() << '\n';

	for (auto& pair : m_connections) {
		Connection& connection = pair.second;
		output << connection.getAddress().getSocketAddressAsString() << '\n';
		output << connection.getAddress().getSocketPortAsString() << '\n';
		output << connection.getUniqueName() << '\n';
	}

	output << m_offeredItems.size() << '\n';

	for (auto& pair : m_offeredItems) {
		Item* item = pair.second;

		output << item->getItemID() << '\n';
		output << item->getDescription() << '\n';
		output << item->getMinimum() << '\n';
		output << item->getCurrentHighest() << '\n';
		output << item->getSeller() << '\n';
		output << item->getHighestBidder() << '\n';

		FILETIME fileTime;
		GetSystemTimeAsFileTime(&fileTime);
		ULARGE_INTEGER* time = reinterpret_cast<ULARGE_INTEGER*>(&fileTime);
		output << time->QuadPart - item->getAuctionStartTime() << '\n';
	}

	output.close();
}

void Server::loadConnections() {
	std::ifstream input("connections.dat");

	if (!input) {
		// if file not found do nothing
		return;
	}

	int32 numConnections = 0;
	input >> numConnections;

	for (int32 i = 0; i < numConnections; i++) {
		std::string ip;
		std::string port;
		std::string name;
		input >> ip;
		input >> port;
		input >> name;

		m_connections[ip] = Connection(name, IPV4Address(ip, port));
	}

	int32 numItems = 0;
	input >> numItems;

	uint32 highestID = 1;

	for (int32 i = 0; i < numItems; i++) {
		uint32 itemId = 0;
		std::string description;
		float32 minimum = 0.0f;
		float32 currentHighest = 0.0f;
		std::string seller;
		std::string highestBidder;
		uint64 time = 0;

		std::string wtfstr;

		input >> itemId;
		std::getline(input, wtfstr); // whyyyyy
		std::getline(input, description);
		input >> minimum;
		input >> currentHighest;
		std::getline(input, wtfstr); // whyyyyy
		std::getline(input, seller);
		std::getline(input, highestBidder);
		input >> time;

		Item item(description, minimum, seller, itemId);
		item.setCurrentHighest(currentHighest);
		item.setHighestBidder(highestBidder);

		if (itemId > highestID) {
			highestID = itemId;
		}
		startAuction(item, 3000000000ull - time);
	}

	input.close();
}