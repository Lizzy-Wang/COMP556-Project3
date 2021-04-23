#ifndef ROUTINGPROTOCOLIMPL_H
#define ROUTINGPROTOCOLIMPL_H

#include "RoutingProtocol.h"
#include "Node.h"
#define SEND_PING 1
#define SEND_DV 2
#define CHECK_STATUS 3

class PortStatusEntry {
  public:
    PortStatusEntry(unsigned short neighbor_id, unsigned short cost, unsigned int last_refreshed_time) {
      this->neighbor_id = neighbor_id;
      this->cost = cost;
      this->last_refreshed_time = last_refreshed_time;
    }
    unsigned short neighbor_id;
    unsigned short cost;
    unsigned int last_refreshed_time;
};

class DVEntry {
  public:
    DVEntry(unsigned short port_id, unsigned short cost, unsigned int last_refreshed_time) {
      this->port_id = port_id;
      this->cost = cost;
      this->last_refreshed_time = last_refreshed_time;
    }
    unsigned short port_id;
    unsigned short cost;
    unsigned int last_refreshed_time;
};

struct Packet {
    uint8_t packet_type;
    uint8_t reserved;
    uint16_t size;
    uint16_t source_id;
    uint16_t dest_id;
    char payload[0];
};

class RoutingProtocolImpl : public RoutingProtocol {
  public:
    RoutingProtocolImpl(Node *n);
    ~RoutingProtocolImpl();

    void init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type);
    // As discussed in the assignment document, your RoutingProtocolImpl is
    // first initialized with the total number of ports on the router,
    // the router's ID, and the protocol type (P_DV or P_LS) that
    // should be used. See global.h for definitions of constants P_DV
    // and P_LS.

    void handle_alarm(void *data);
    // As discussed in the assignment document, when an alarm scheduled by your
    // RoutingProtoclImpl fires, your RoutingProtocolImpl's
    // handle_alarm() function will be called, with the original piece
    // of "data" memory supplied to set_alarm() provided. After you
    // handle an alarm, the memory pointed to by "data" is under your
    // ownership and you should free it if appropriate.

    void recv(unsigned short port, void *packet, unsigned short size);
    // When a packet is received, your recv() function will be called
    // with the port number on which the packet arrives from, the
    // pointer to the packet memory, and the size of the packet in
    // bytes. When you receive a packet, the packet memory is under
    // your ownership and you should free it if appropriate. When a
    // DATA packet is created at a router by the simulator, your
    // recv() function will be called for such DATA packet, but with a
    // special port number of SPECIAL_PORT (see global.h) to indicate
    // that the packet is generated locally and not received from 
    // a neighbor router.

 private:
    // check the status of DV entries and port status entries every 1 second
    void periodic_check_status();
    // send ping packet to all the neighbors every 10 seconds
    void periodic_send_PING();
    // call send_DV() every 30 seconds
    void periodic_send_DV();
    // send dv update packet to all linked neighbors
    void send_DV();
    // recv different packets
    void recv_data(unsigned short port, Packet *packet, unsigned short size);
    void recv_PING(unsigned short port, Packet *packet, unsigned short size);
    void recv_PONG(unsigned short port, Packet *packet,unsigned short size);
    void recv_DV(unsigned short port, Packet *packet, unsigned short size);
    void recv_LS(unsigned short port, Packet *packet, unsigned short size);

    Node *sys; // To store Node object; used to access GSR9999 interfaces 
    unsigned short num_ports;
    unsigned short router_id;
    eProtocolType protocol_type;

    unordered_map<unsigned short, PortStatusEntry *> port_status_map;        
    // the function of forwardin_map is already realized by DV_map if we only implement one protocal
    // unordered_map<unsigned short, unsigned short> forwarding_map;   
    unordered_map<unsigned short, DVEntry *> DV_map;       
};

#endif

