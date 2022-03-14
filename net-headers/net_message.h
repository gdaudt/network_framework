#pragma once    
#include "net_common.h"

template <typename T>
struct message_header{
    T id{};
    uint32_t size = 0;
};

// Message Body contains a header and a std::vector, containing raw bytes
// of infomation. Message has variable length but should update the header size.
template <typename T>
struct message{
    // Header & Body vector
    message_header<T> header{};
    std::vector<uint8_t> body;

    // returns size of entire message packet in bytes
    size_t size() const{
        return body.size();
    }

    // Override for std::cout compatibility - produces friendly description of message
    friend std::ostream& operator << (std::ostream& os, const message<T>& msg){
        os << "ID:" << int(msg.header.id) << " Size:" << msg.header.size;
        return os;
    }

    // Convenience Operator overloads - Add and remove data from the body vector as if it were a stack
    // First In, Last Out. Only accepts simple types, has to be able to serialize/de-serialize them from the vector
    // Takes a template as a parameter to accept every possible type we might want to insert into messages.

    // Pushing data into the body vector.
    template<typename DataType>
    friend message<T>& operator << (message<T>& msg, const DataType& data){
        // Check that the type of the data being pushed is trivially copyable
        static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pushed into vector");

        // Cache current size of vector, as this will be the point we insert the data
        size_t i = msg.body.size();

        // Resize the vector by the size of the data being pushed
        msg.body.resize(msg.body.size() + sizeof(DataType));

        // Physically copy the data into the newly allocated vector space
        std::memcpy(msg.body.data() + i, &data, sizeof(DataType));

        // Recalculate the message size
        msg.header.size = msg.size();

        return msg;
    }

    // Pulling data from the body vector.
    template<typename DataType>
    friend message<T>& operator >> (message<T>& msg, DataType& data){
        // Check that the type of the data being pushed is trivially copyable
        static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pulled from vector");

        // Cache the location towards the end of the vector where the pulled data starts
        size_t i = msg.body.size() - sizeof(DataType);

        // Physically copy the data from the vector into the user variable
        std::memcpy(&data, msg.body.data() + i, sizeof(DataType));

        // Shrink the vector to remove read bytes, and reset end position
        msg.body.resize(i);

        // Recalculate the message size
        msg.header.size = msg.size();

        // Return the target message so it can be "chained"
        return msg;
    }			
};

template <typename T>
class connection;

template <typename T>
struct owned_message{
    std::shared_ptr<connection<T>> remote = nullptr;
    message<T> msg;

    // friendly string maker
    friend std::ostream& operator<<(std::ostream& os, const owned_message<T>& msg){
        os << msg.msg;
        return os;
    }
};