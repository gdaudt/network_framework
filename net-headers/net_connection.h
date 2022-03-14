#pragma once

#include "net_common.h"
#include "net_tsqueue.h"
#include "net_message.h"

template<typename T>
class server_interface;

template<typename T>
    class connection : public std::enable_shared_from_this<connection<T>>{
    public:
        // A connection is "owned" by either a server or a client, and its
        // behaviour is slightly different bewteen the two.
        enum class owner{
            server,
            client
        };

    public:
        // Constructor: Specify Owner, connect to context, transfer the socket
        //				Provide reference to incoming message queue
        connection(owner parent, asio::io_context& asioContext, asio::ip::tcp::socket socket, tsqueue<owned_message<T>>& qIn)
            : m_asioContext(asioContext), m_socket(std::move(socket)), m_qMessagesIn(qIn){
            
            m_nOwnerType = parent;

            // Construct validation check data
            if (m_nOwnerType == owner::server){
                // Connection is Server -> Client, construct random data for the client
                // to transform and send back for validation
                // might be subject to change in case chrono funcion proves inconsistent
                m_nHandshakeOut = uint64_t(std::chrono::system_clock::now().time_since_epoch().count());

                // Pre-calculate the result for checking when the client responds
                m_nHandshakeCheck = scramble(m_nHandshakeOut);
            }
            else{
                // Connection is Client -> Server, so we have nothing to define, 
                m_nHandshakeIn = 0;
                m_nHandshakeOut = 0;
            }
        }

        virtual ~connection()
        {}

        // This ID is used system wide - its how clients will be identified individually
        uint32_t GetID() const{
            return id;
        }

    public:
        void ConnectToClient(server_interface<T>* server, uint32_t uid = 0){
            if (m_nOwnerType == owner::server){
                if (m_socket.is_open()){
                    id = uid;                   
                    // A client has attempted to connect to the server, but we wish
                    // the client to first validate itself. Write the data for validation
                    WriteValidation();

                    // Wait asynchronously for the validation data sent back from the client
                    ReadValidation(server);
                }
            }
        }

        void ConnectToServer(const asio::ip::tcp::resolver::results_type& endpoints){
            // Only clients can connect to servers
            if (m_nOwnerType == owner::client){
                // Request asio attempts to connect to an endpoint
                asio::async_connect(m_socket, endpoints,
                    [this](std::error_code ec, asio::ip::tcp::endpoint endpoint){
                        if (!ec){                            
                            // First thing server will do is send packet to be validated
                            // so wait for that and respond
                            ReadValidation();
                        }
                    });
            }
        }


        void Disconnect(){
            if (IsConnected())
                asio::post(m_asioContext, [this]() { m_socket.close(); });
        }

        bool IsConnected() const{
            return m_socket.is_open();
        }

        // Prime the connection to wait for incoming messages
        void StartListening(){
            
        }

    public:
        // ASYNC - Send a message, connections are one-to-one so no need to specifiy
        // the target, for a client, the target is the server and vice versa
        void Send(const message<T>& msg){
            asio::post(m_asioContext,
                [this, msg](){
                    // If the queue has a message in it, then we must 
                    // assume that it is in the process of asynchronously being written.
                    // Either way add the message to the queue to be output. If no messages
                    // were available to be written, then start the process of writing the
                    // message at the front of the queue.
                    bool bWritingMessage = !m_qMessagesOut.empty();
                    m_qMessagesOut.push_back(msg);
                    if (!bWritingMessage){
                        WriteHeader();
                    }
                });
        }



    private:
        // ASYNC - Prime context to write a message header
        void WriteHeader(){
            // Queue has a message to send, allocate a transmission buffer to hold
            // the message, and issue the work
            asio::async_write(m_socket, asio::buffer(&m_qMessagesOut.front().header, sizeof(message_header<T>)),
                [this](std::error_code ec, std::size_t length){
                    // asio has now sent the bytes - if there was a problem
                    // an error would be available
                    if (!ec){
                        // no error, so check if the message header just sent also
                        // has a message body
                        if (m_qMessagesOut.front().body.size() > 0){
                            // issue the task to write the body bytes
                            WriteBody();
                        }
                        else{
                            // no body, so we are done with this message. Remove it from 
                            // the outgoing message queue
                            m_qMessagesOut.pop_front();

                            // If the queue is not empty, there are more messages to send, so
                            // make this happen by issuing the task to send the next header.
                            if (!m_qMessagesOut.empty()){
                                WriteHeader();
                            }
                        }
                    }
                    else{
                        // asio failed to write the message, assume the connection has died by closing the
                        // socket. When a future attempt to write to this client fails due
                        // to the closed socket, it will be tidied up.
                        std::cout << "[" << id << "] Write Header Fail.\n";
                        m_socket.close();
                    }
                });
        }

        // ASYNC - Prime context to write a message body
        void WriteBody(){
            // If this function is called, a header has just been sent, and that header
            // indicated a body existed for this message. Fill a transmission buffer
            // with the body data, and send it
            asio::async_write(m_socket, asio::buffer(m_qMessagesOut.front().body.data(), m_qMessagesOut.front().body.size()),
                [this](std::error_code ec, std::size_t length){
                    if (!ec){
                        // Sending was successful, so we are done with the message
                        // and remove it from the queue
                        m_qMessagesOut.pop_front();

                        // If the queue still has messages in it, then issue the task to 
                        // send the next messages' header.
                        if (!m_qMessagesOut.empty()){
                            WriteHeader();
                        }
                    }
                    else{
                        // Sending failed, see WriteHeader() equivalent for description
                        std::cout << "[" << id << "] Write Body Fail.\n";
                        m_socket.close();
                    }
                });
        }

        // ASYNC - Prime context ready to read a message header
        void ReadHeader(){
            // If this function is called, we are expecting asio to wait until it receives
            // enough bytes to form a header of a message. Allocate a transmission buffer large enough to store it. 
            // we construct the message in a "temporary" message object as it's convenient to work with.
            asio::async_read(m_socket, asio::buffer(&m_msgTemporaryIn.header, sizeof(message_header<T>)),
                [this](std::error_code ec, std::size_t length){						
                    if (!ec){
                        // A complete message header has been read, check if this message
                        // has a body to follow.
                        if (m_msgTemporaryIn.header.size > 0){
                            // allocate enough space in the messages' body
                            // vector, and issue asio with the task to read the body.
                            m_msgTemporaryIn.body.resize(m_msgTemporaryIn.header.size);
                            ReadBody();
                        }
                        else{
                            // it doesn't, so add this bodyless message to the connections
                            // incoming message queue
                            AddToIncomingMessageQueue();
                        }
                    }
                    else{
                        // Reading form the client went wrong, most likely a disconnect
                        // has occurred. Close the socket and let the system tidy it up later.
                        std::cout << "[" << id << "] Read Header Fail.\n";
                        m_socket.close();
                    }
                });
        }

        // ASYNC - Prime context ready to read a message body
        void ReadBody(){
            // If this function is called, a header has already been read, and that header
            // request we read a body, The space for that body has already been allocated
            // in the temporary message object, so just wait for the bytes to arrive
            asio::async_read(m_socket, asio::buffer(m_msgTemporaryIn.body.data(), m_msgTemporaryIn.body.size()),
                [this](std::error_code ec, std::size_t length){						
                    if (!ec){
                        // the message is now complete, so add
                        // the whole message to incoming queue
                        AddToIncomingMessageQueue();
                    }
                    else{
                        // Same error logic follows                        
                        std::cout << "[" << id << "] Read Body Fail.\n";
                        m_socket.close();
                    }
                });
        }

        // "Encrypt" data
        uint64_t scramble(uint64_t nInput){
            uint64_t out = nInput ^ 0xDEADBEEFC0DECAFE;
            out = (out & 0xF0F0F0F0F0F0F0) >> 4 | (out & 0x0F0F0F0F0F0F0F) << 4;
            return out ^ 0xC0DEFACE12345678;
        }

        // ASYNC - Used by both client and server to write validation packet
        void WriteValidation(){
            asio::async_write(m_socket, asio::buffer(&m_nHandshakeOut, sizeof(uint64_t)),
                [this](std::error_code ec, std::size_t length){
                    if (!ec){
                        // Validation data sent, clients should sit and wait
                        // for a response (or a closure)
                        if (m_nOwnerType == owner::client)
                            ReadHeader();
                    }
                    else{
                        m_socket.close();
                    }
                });
        }

        void ReadValidation(server_interface<T>* server = nullptr){
            asio::async_read(m_socket, asio::buffer(&m_nHandshakeIn, sizeof(uint64_t)),
                [this, server](std::error_code ec, std::size_t length){
                    if (!ec){
                        if (m_nOwnerType == owner::server){
                            // Connection is a server, so check response from client

                            // Compare sent data to actual solution
                            if (m_nHandshakeIn == m_nHandshakeCheck){
                                // Client has provided valid solution, so allow it to connect properly
                                std::cout << "Client Validated" << std::endl;
                                server->OnClientValidated(this->shared_from_this());

                                // Sit waiting to receive data now
                                ReadHeader();
                            }
                            else{
                                // Client gave incorrect data, so disconnect
                                std::cout << "Client Disconnected (Fail Validation)" << std::endl;
                                m_socket.close();
                            }
                        }
                        else{
                            // Connection is a client, so solve puzzle
                            m_nHandshakeOut = scramble(m_nHandshakeIn);

                            // Write the result
                            WriteValidation();
                        }
                    }
                    else{
                        // Some biggerfailure occured
                        std::cout << "Client Disconnected (ReadValidation)" << std::endl;
                        m_socket.close();
                    }
                });
        }

        // Once a full message is received, add it to the incoming queue
        void AddToIncomingMessageQueue(){				
            // Shove it in queue, converting it to an "owned message", by initialising
            // with the a shared pointer from this connection object
            if(m_nOwnerType == owner::server)
                m_qMessagesIn.push_back({ this->shared_from_this(), m_msgTemporaryIn });
            else
                m_qMessagesIn.push_back({ nullptr, m_msgTemporaryIn });

            // Prime asio context to receive more messages. Message construction
            // process repeats itself.
            ReadHeader();
        }

    protected:
        // Each connection has a unique socket to a remote 
        asio::ip::tcp::socket m_socket;

        // This context is shared with the whole asio instance
        asio::io_context& m_asioContext;

        // This queue holds all messages to be sent to the remote side
        // of this connection
        tsqueue<message<T>> m_qMessagesOut;

        // This references the incoming queue of the parent object
        tsqueue<owned_message<T>>& m_qMessagesIn;

        // Incoming messages are constructed asynchronously, so we will
        // store the part assembled message here, until it is ready
        message<T> m_msgTemporaryIn;

        // The "owner" decides how some of the connection behaves
        owner m_nOwnerType = owner::server;

        // Handshake Validation			
        uint64_t m_nHandshakeOut = 0;
        uint64_t m_nHandshakeIn = 0;
        uint64_t m_nHandshakeCheck = 0;


        bool m_bValidHandshake = false;
        bool m_bConnectionEstablished = false;

        uint32_t id = 0;

};