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
};

class CustomClient : public client_interface<CustomMsgTypes>{
public:
	int battery = 170;
	void PingServer(){
		message<CustomMsgTypes> msg;
		msg.header.id = CustomMsgTypes::ServerPing;

		// Caution with this...
		std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();		

		msg << timeNow;
		Send(msg);
	}

	void MessageAll(){
		message<CustomMsgTypes> msg;
		msg.header.id = CustomMsgTypes::MessageAll;		
		Send(msg);
	}

	void RequestGoal(){
		message<CustomMsgTypes> msg;
		std::cout << "Reached goal, requesting new one." << std::endl;
		msg.header.id = CustomMsgTypes::RobotGoalRequest;
		Send(msg);
	}	

	void RequestStation(){
		message<CustomMsgTypes> msg;
		std::cout << "Low battery, requesting a charging station location." << std::endl;
		msg.header.id = CustomMsgTypes::RobotLowBattery;
		Send(msg);
	}

	void RequestPath(){
		message<CustomMsgTypes> msg;
		std::cout << "Requesting new path." << std::endl;
		msg.header.id = CustomMsgTypes::RobotPathRequest;
		Send(msg);
	}
};

int main(){
	uint32_t currentX, currentY;
	CustomClient c;
	std::vector<int> goals;
	c.Connect("127.0.0.1", 60000);	
	while(true){				
		if (c.IsConnected()){				
			if (!c.Incoming().empty()){

				auto msg = c.Incoming().pop_front().msg;

				switch (msg.header.id){
					case CustomMsgTypes::ServerAccept:{
						// Server has responded to a ping request				
						std::cout << "Server Accepted Connection\n";
						c.RequestPath();
					}
					break;
					case CustomMsgTypes::ServerMessage:{
						// Server has responded to a ping request	
						uint32_t clientID;
						msg >> clientID;
						std::cout << "Hello from [" << clientID << "]\n";
					}
					break;
					case CustomMsgTypes::ServerNewGoal:{
						msg >> currentY >> currentX;
						std::cout << "New goal is: (" << currentX << "," << currentY << ")" << std::endl;
						sleep(4);
						if(c.battery <= 10){
							c.RequestStation();
						}
						else{
							c.RequestPath();						
						}					
					}
					break;	
					case CustomMsgTypes::ServerCharge:{
						msg >> currentY >> currentX;
						std::cout << "Charging station is: (" << currentX << "," << currentY << ")" << std::endl;
						sleep(2);
						std::cout<< "Charging..." << std::endl;
						sleep(6);
						c.battery = 170;
						c.RequestPath();
					}
					break;

					case CustomMsgTypes::ServerNewPath:{
						std::cout << "Receiving path. ";
						msg >> currentY >> currentX;
						goals.push_back(currentX);
						goals.push_back(currentY);
						std::cout << "Adding (" << currentX << "," << currentY << ")" << std::endl;
						c.RequestPath();
					}
					break;

					case CustomMsgTypes::ServerPathDone:{
						std::cout << "Received new path, following it now." << std::endl;	
						std::reverse(goals.begin(), goals.end());															
						for(int i = 0; i<goals.size(); i+2){
							currentX = goals.back();
							goals.pop_back();
							currentY = goals.back();
							goals.pop_back();
							std::cout << "Next goal: (" << currentX << "," << currentY << ")" << std::endl;
							sleep(4);
							std::cout << "Reached goal." << std::endl;
							c.battery -= 10;
							std::cout << "Battery: " << c.battery << std::endl;
						}
						if(c.battery <= 50){
							c.RequestStation();
						}
						else{
							c.RequestPath();						
						}	
					}
					break;
				}
			}
		}
	}
	return 0;		
}