#pragma once
#include "Session.h"
#include <iostream>
#include "Protocol.h"





class PacketHandler
{
public:
	static void HandlePacket(Session* session, PacketHeader* header, char* buffer)
	{
		if (buffer == nullptr) {
			std::cout << "[Error] Buffer is null!" << std::endl;
			return;
		}

		std::cout << "[Debug] Packet Received - ID: " << header->id << ", Size: " << header->size << std::endl;
		

		switch (header->id)
		{
		case PKT_C_LOGIN:
			Handle_C_LOGIN(session, buffer);
			break;

		case PKT_S_LOGIN:
			Handle_S_LOGIN(session, buffer);
			break; 

		case PKT_C_MOVE:
			Handle_C_MOVE(session, buffer);
			break;

		case PKT_C_ENTER_GAME:
			Handle_C_ENTER_GAME(session, buffer);
			break;

		}

	}


private:
	static void Handle_C_LOGIN(Session* session, char* buffer);

	static void Handle_C_MOVE(Session* session, char* buffer);

	static void Handle_C_ENTER_GAME(Session* session, char* buffer);

	static void Handle_S_LOGIN(Session* session, char* buffer);
};

