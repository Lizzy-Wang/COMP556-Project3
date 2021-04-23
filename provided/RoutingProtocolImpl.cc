#include <netinet/in.h>
#include "RoutingProtocolImpl.h"

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) {
  sys = n;
}

RoutingProtocolImpl::~RoutingProtocolImpl() {
    for (auto iter = port_status_map.begin(); iter != port_status_map.end(); iter++) {
      delete iter->second;
    }
    for (auto iter = DV_map.begin(); iter != DV_map.end(); iter++) {
      delete iter->second;
    }
}

void RoutingProtocolImpl::init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type) {
  this->num_ports = num_ports;
  this->router_id = router_id;
  this->protocol_type = protocol_type;


  for (int i = 0; i < num_ports; i++) {
      port_status_map[i] = new PortStatusEntry(router_id, INFINITY_COST, 0);
  }

  periodic_send_PING();
  periodic_send_DV();
  periodic_check_status();
}

void RoutingProtocolImpl::handle_alarm(void *data) {
  switch (((int *) data)[0]) {
      case SEND_PING:
          periodic_send_PING();
          break;
      case SEND_DV:
          periodic_send_DV();
          break;
      case CHECK_STATUS:
          periodic_check_status();
          break;
      default:
          break;
  }
  delete (int *) data;
}

void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size) {
    Packet *p = (Packet *) packet;
    switch (p->packet_type) {
        case DATA:
            recv_data(port, p, size);
            break;
        case PING:
            recv_PING(port, p, size);
            break;
        case PONG:
            recv_PONG(port, p, size);
            break;
        case DV:
            recv_DV(port, p, size);
            break;
        case LS:
            recv_LS(port, p, size);
            break;
        default:
            break;
    }
}

void RoutingProtocolImpl::periodic_send_PING() {
  for (int i = 0; i < num_ports; i++) {
    // 32 bit payload
    unsigned short size = sizeof(Packet) + 4;
    Packet *packet = (Packet *) malloc(size);
    unsigned int curr_time = sys->time();
    packet->packet_type = PING;
    packet->source_id = htons(router_id);
    packet->size = htons(size);
    *((uint32_t *) packet->payload) = htonl(curr_time);
    sys->send(i, packet, size);
  }

  int *data = new int[1];
  data[0] = SEND_PING;
  sys->set_alarm(this, 10000, data);
}

void RoutingProtocolImpl::periodic_send_DV() {
  send_DV();

  int *data = new int[1];
  data[0] = SEND_PING;
  sys->set_alarm(this, 30000, data);
}

void RoutingProtocolImpl::send_DV() {
    if (DV_map.size() == 0) {
      return;
    }
    // 32 bit per dv entry in packet payload
    unsigned short packet_size = sizeof(Packet) + DV_map.size() * 4;
    for (int i = 0; i < num_ports; i++) {
      if (port_status_map[i]->neighbor_id == router_id || port_status_map[i]->cost == INFINITY_COST) {
        continue;
      }
        
      Packet *dv_update_packet = (Packet *) malloc(packet_size);
      dv_update_packet->packet_type = DV;
      dv_update_packet->size = htons(packet_size);
      dv_update_packet->source_id = htons(router_id);
      unsigned short dest_id = port_status_map[i]->neighbor_id;
      dv_update_packet->dest_id = htons(dest_id);

      // set payload <Node_Id, Cost> pairs in dv_update_packet
      uint16_t *payload_ptr = (uint16_t *)dv_update_packet->payload;
      for (auto iter = DV_map.begin(); iter != DV_map.end(); iter++) {
            // destination node id
            *payload_ptr = (uint16_t)htons(iter->first);
            payload_ptr++;

            // cost 
            if (iter->second->port_id == i) {
              // poison reverse
              *payload_ptr = (uint16_t)htons(INFINITY_COST);
              payload_ptr++;
            } else {
              *payload_ptr = (uint16_t)htons(iter->second->cost);
              payload_ptr++;
            }
      }
    }
}

void RoutingProtocolImpl::periodic_check_status() {
  unsigned int curr_time = sys->time();
  bool isDVUpdated = true;

  // check the status of port_status_map
  for (auto iter1 = port_status_map.begin(); iter1 != port_status_map.end(); iter1++) {
      PortStatusEntry *port_status = iter1->second;
      if (port_status->neighbor_id == router_id || port_status->cost == INFINITY_COST) {
        continue;
      }
      if (curr_time - port_status->last_refreshed_time >= 15000) {
        port_status->cost = INFINITY_COST;
        // update dv: delete entry with port as next hop from DV_map
        auto iter2 = DV_map.begin();
        while (iter2 != DV_map.end()) {
            if (iter2->second->port_id == iter1->first) {
              delete iter2->second;
              DV_map.erase(iter2++);
              isDVUpdated = true;
            } else {
              iter2++;
            }
        } 
      }
  }

  // check the status of DV_map
  auto iter = DV_map.begin();
  while (iter != DV_map.end()) {
      DVEntry *dv = iter->second;
      // delete out-of-time entry from DV_map
      if (curr_time - dv->last_refreshed_time >= 45000) {
        delete dv;
        DV_map.erase(iter++);
        isDVUpdated = true;
      } else {
        iter++;
      }
  } 

  if (isDVUpdated) {
    send_DV();
  }

  int *data = new int[1];
  data[0] = SEND_PING;
  sys->set_alarm(this, 1000, data);
}

void RoutingProtocolImpl::recv_data(unsigned short port, Packet *packet, unsigned short size) {
    unsigned short dest_id = ntohs(packet->dest_id);
    // data packtet arrives at destination
    if (dest_id == router_id) {
        delete packet;
        return;
    }
    // otherwise, find the next hop in forwarding map
    if (DV_map.find(dest_id) != DV_map.end()) {
        sys->send(DV_map[dest_id]->port_id, packet, size);    
        return;
    }
    delete packet;
}

void RoutingProtocolImpl::recv_PING(unsigned short port, Packet *packet, unsigned short size) {
   // send PONG
   packet->packet_type = PONG;
   packet->source_id = htons(router_id);
   packet->dest_id = packet->source_id;
   sys->send(port, packet, size);
}

void RoutingProtocolImpl::recv_PONG(unsigned short port, Packet *packet, unsigned short size) {
  // update port status entry
  uint32_t ping_time = ntohl(*((uint32_t *)packet->payload));
  uint32_t curr_time = sys->time();
  unsigned short neighbor_id = ntohs(packet->source_id);
  PortStatusEntry *port_entry = port_status_map[port];
  unsigned int prev_cost = port_entry->cost;
  port_entry->neighbor_id = neighbor_id;
  port_entry->cost = curr_time - ping_time;
  port_entry->last_refreshed_time = curr_time;

  // update DV map
  bool isDVUpdated = false;
  // case 1: new link 
  if (DV_map.find(neighbor_id) == DV_map.end()) {
      // add entry to DV map with healed neighbor
      DV_map[neighbor_id] = new DVEntry(port, port_entry->cost, curr_time);
      isDVUpdated = true;
  } else {
    // case 2: old link with updated cost
    auto iter = DV_map.begin();
    while (iter != DV_map.end()) {
        DVEntry *dv_entry = iter->second;
        // updated port is the next port
        if (dv_entry->port_id == port) {
          if (iter->first == neighbor_id) {
              dv_entry->cost = port_entry->cost;
          } else {
              dv_entry->cost += port_entry->cost - prev_cost;
          }
          isDVUpdated = true;
        } else {
          // updated port is not the next port
          // if the cost is smaller between neighbor node and current node
          if (iter->first == neighbor_id || dv_entry->cost > port_entry->cost) {
              dv_entry->cost = port_entry->cost;
              isDVUpdated = true;
          }
        }
        iter++;
    } 
  }
  
  if (isDVUpdated) {
    send_DV();
  }
  delete packet;
}

void RoutingProtocolImpl::recv_DV(unsigned short port, Packet *packet, unsigned short size) {
  // reconstruct recv packet to map with pair <destination id, cost>
  unordered_map<unsigned short, unsigned short> recv_dv_map;
  uint16_t *payload_ptr = (uint16_t *)(packet->payload);
  unsigned short entry_num = (packet->size - sizeof(Packet))/4;
  for (int i = 0; i < entry_num; i++) {
      uint16_t dest_id = ntohs(*payload_ptr);
      payload_ptr++;
      uint16_t cost = ntohs(*payload_ptr);
      payload_ptr++;
      recv_dv_map[dest_id] = cost;
  }

  // flag showing DV map is updated or not
  bool isDVUpdated = false;
  unsigned int curr_time = sys->time();

  // update router's DV_map with received dv map from neighbor
  for (auto iter = recv_dv_map.begin(); iter != recv_dv_map.end(); iter++) {
      unsigned short recv_dest_id = iter->first;
      unsigned short recv_cost = iter->second;
      // don't need to store the dv entry leading to itself
      if (recv_dest_id == router_id) {
        continue;
      }
      // poison reverse, ignore
      if (recv_cost == INFINITY_COST) {
        continue;
      }
      // case1: new destination node: add new entry to dv map
      if (DV_map.find(recv_dest_id) == DV_map.end()) {
          unsigned short total_cost = port_status_map[port]->cost + recv_cost;
          DV_map[recv_dest_id] = new DVEntry(port, total_cost, curr_time);
          isDVUpdated = true;
      } else {
          // case2: exiting destination node: check cost; if smaller, update entry
          unsigned short prev_cost = DV_map[recv_dest_id]->cost;
          unsigned short curr_cost = port_status_map[port]->cost + recv_cost;
          if (prev_cost > curr_cost || DV_map[recv_dest_id]->port_id == port) {
            DV_map[recv_dest_id]->port_id = port;
            DV_map[recv_dest_id]->cost = curr_cost;
            DV_map[recv_dest_id]->last_refreshed_time = curr_time;
            isDVUpdated = true;
          }
      }
  }
  // delete entry with next hop that equals port if destination node isn't found in packet
  auto iter = DV_map.begin();
  while (iter != DV_map.end()) {
    unsigned short dest_id = iter->first;
    DVEntry *dv = iter->second;
    if (recv_dv_map.find(dest_id) == recv_dv_map.end() && dv->port_id == port) {
      delete dv;
      DV_map.erase(iter++);
      isDVUpdated = true;
    } else {
      iter++;
    }
  } 

  if (isDVUpdated) {
    send_DV();
  }
  delete packet;
}

void RoutingProtocolImpl::recv_LS(unsigned short port, Packet *packet, unsigned short size) {
  return;
}

