#pragma once

#include "net_common.h"
#include "net_message.h"
#include "net_tsqueue.h"
#include "net_connection.h"


template<typename T>
class client_interface{
    
    public:
        client_interface() : m_socket(m_context){
            // Initialize the socket with the io context, so it can do stuff
        }

        virtual ~client_interface(){
            // If the client is destroyed, always try to disconnect from server
            Disconnect();
        }

    public:
        // Connect to server with hostname/ip-address and port
        bool Connect(const std::string& host, const uint16_t port){
            try{
                // Resolve hostname/ip-address into tangible physical address
                asio::ip::tcp::resolver resolver(m_context);
                asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));
                
                //Create connection
                m_connection = std::make_unique<connection<T>>(
                    connection<T>::owner::client,
                    m_context,
                    asio::ip::tcp::socket(m_context), m_qMessagesIn);         

                //Tell the connection object to connect to server
                m_connection->ConnectToServer(endpoints);

                // Start context thread
                thrContext = std::thread([this]() { m_context.run(); });
            }
            catch (std::exception& e){
                std::cerr << "Client Exception: " << e.what() << std::endl;
                return false;
            }
            return false;
        }

        void Disconnect(){
            if(IsConnected()){
                m_connection->Disconnect();
            }

            m_context.stop();
            if(thrContext.joinable())
                thrContext.join();

            m_connection.release();
        }

        // Check if client is actually connected to server
        bool IsConnected(){
            if(m_connection)
                return m_connection->IsConnected();
            else
                return false;
        }     

        // Send message to server
        void Send(const message<T>& msg){
            if (IsConnected())
                    m_connection->Send(msg);
        }

        // Retrieve queue of messages from server
        tsqueue<owned_message<T>>& Incoming(){ 
            return m_qMessagesIn;
        }

        void Wait(){
            m_qMessagesIn.wait();
        }

    protected:
        // asio context handles the data transfer
        asio::io_context m_context;
        // but needs a thread of its own to execute its work commands
        std::thread thrContext;
        // This is the hardware socket that is connected to the server
        asio::ip::tcp::socket m_socket;
        // The client has a single instance of a "connection" object, which handles data transfer
        std::unique_ptr<connection<T>> m_connection;
    
    private:
        // This is the thread safe queue of incoming messages from server
        tsqueue<owned_message<T>> m_qMessagesIn;
};