#include <iostream>
#include "net-headers/net_framework.h"

enum class CustomMsgTypes : uint32_t{
	ServerAccept,
	ServerDeny,
	ServerPing,
	MessageAll,
	ServerMessage,
	ServerNewGoal,
	ServerNewPath,
	ServerPathDone,
	ServerCharge,
	RobotGoalRequest,
	RobotLowBattery,
	RobotPathRequest,
	RobotPathDone
};

struct Goal{
	float x;
	float y;
};

// addJaguarGoal(-2.034,5.32,0.25,0.96);
// addJaguarGoal(3.508,7.143,0.255,0.966);
// // meio apontando para POA
// addJaguarGoal(31.131,22.277,0.252,0.967);
// addJaguarGoal(32.005,22.718,0.254,0.967);
// // B 
// addJaguarGoal(25.06,21.31,-0.96,0.25);
// // meio apontando para centro de viamao
// addJaguarGoal(0.09,7.65,-0.96,0.25);

class CustomServer : public server_interface<CustomMsgTypes>{
public:
	std::vector<Goal> goals[10000];
	int pathCounter[10000];
	int maxPath = 5;
	CustomServer(uint16_t nPort) : server_interface<CustomMsgTypes>(nPort){

	}

protected:
	virtual bool OnClientConnect(std::shared_ptr<connection<CustomMsgTypes>> client){		
		return true;
	}

	void OnClientValidated(std::shared_ptr<connection<CustomMsgTypes>> client){
		message<CustomMsgTypes> msg;
		msg.header.id = CustomMsgTypes::ServerAccept;
		client->Send(msg);
		std::cout << "id: " << client->GetID() << std::endl;
		pathCounter[client->GetID()] = maxPath;
		std::vector<Goal> vg;
		Goal g;
		g.x = -2.034;
		g.y = 6.32;
		vg.push_back(g);
		g.x = 11.6;
		g.y = 11.7;
		vg.push_back(g);
		g.x = 31.131;
		g.y = 22.277;
		vg.push_back(g);
		g.x = 32.005;
		g.y = 22.718;
		vg.push_back(g);
		g.x = 25.06;
		g.y = 21.31;
		vg.push_back(g);
		std::reverse(vg.begin(), vg.end());
		goals[client->GetID()] = vg;
		pathCounter[client->GetID()] = 5;
	}

	// Called when a client appears to have disconnected
	virtual void OnClientDisconnect(std::shared_ptr<connection<CustomMsgTypes>> client){
		std::cout << "Removing client [" << client->GetID() << "]\n";
	}

	// Called when a message arrives
	virtual void OnMessage(std::shared_ptr<connection<CustomMsgTypes>> client, message<CustomMsgTypes>& msg){
		switch (msg.header.id)
		{
			case CustomMsgTypes::ServerPing:
			{
				std::cout << "[" << client->GetID() << "]: Server Ping\n";

				// Simply bounce message back to client
				client->Send(msg);
			}
			break;

			case CustomMsgTypes::MessageAll:
			{
				std::cout << "[" << client->GetID() << "]: Message All\n";

				// Construct a new message and send it to all clients
				message<CustomMsgTypes> msg;
				msg.header.id = CustomMsgTypes::ServerMessage;
				msg << client->GetID();
				MessageAllClients(msg, client);
			}
			break;

			case CustomMsgTypes::RobotGoalRequest:{			
				std::cout << "[" << client->GetID() << "]: Robot Reached goal, sending new one. ";		

				message<CustomMsgTypes> newMsg;
				newMsg.header.id = CustomMsgTypes::ServerNewGoal;
				uint32_t x = rand() % 100;
				uint32_t y = rand() % 100;
				newMsg << x << y;		
				std::cout << "Sending (" << x << "," << y << ")" << std::endl;	
				client->Send(newMsg);			
			}
			break;

			case CustomMsgTypes::RobotPathRequest:{
				std::cout << "[" << client->GetID() << "]: Robot requested a path, sending goals." << std::endl; 

				message<CustomMsgTypes> newMsg;
				if(pathCounter[client->GetID()] <= 0){
					newMsg.header.id = CustomMsgTypes::ServerPathDone;
					client->Send(newMsg);
					pathCounter[client->GetID()] = maxPath;
				}
				else{
					newMsg.header.id = CustomMsgTypes::ServerNewPath;					
					Goal g = goals[client->GetID()].back();
					goals[client->GetID()].pop_back();
 					std::cout << "Adding (" << g.x << "," << g.y << ")" << std::endl;
					float x,y;
					x = g.x;
					y = g.y;
					newMsg << x << y;
					client->Send(newMsg);
					pathCounter[client->GetID()] -= 1;				
				}
							
			}
			break;

			case CustomMsgTypes::RobotPathDone:{
				std::cout << "[" << client->GetID() << "] Robot has received his path." << std::endl;
			}
			break;
			
			case CustomMsgTypes::RobotLowBattery:{
				std::cout << "[" << client->GetID() << "]: Robot is low on battery, sending to charging station. ";

				message<CustomMsgTypes> newMsg;
				newMsg.header.id = CustomMsgTypes::ServerCharge;
				uint32_t x = rand() % 10;
				uint32_t y = rand() % 10;
				newMsg << x << y;
				std::cout << "Sending (" << x << "," << y << ")" << std::endl;	
				client->Send(newMsg);
			}
			break;
		}
	}
};

int main()
{
	CustomServer server(60000);
	server.Start();

	while (1){
		server.Update(-1, true);
	}

	return 0;
}