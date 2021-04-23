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
  


  if (protocol_type == P_DV) {
      cout<< "DEBUG: Protocal is "<<protocol_type<<endl;
      periodic_send_PING();
      periodic_send_DV();
      periodic_check_status();
  } else {
     // TO DO
      return;
  }
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
  // cout<<"DEBUG: send PING from router "<<router_id<<endl;
  for (int i = 0; i < num_ports; i++) {
    // 32 bit payload
    unsigned short size = sizeof(Packet) + 4;
    Packet *packet = (Packet *) malloc(size);
    unsigned int curr_time = sys->time();
    // cout<<"DEBUG: send PING curr_time "<<curr_time<<endl;
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
  cout<< "DEBUG: periodic_send_DV on router " << router_id<<endl;
  send_DV();

  int *data = new int[1];
  data[0] = SEND_DV;
  sys->set_alarm(this, 30000, data);
}

void RoutingProtocolImpl::send_DV() {
    cout<< "DEBUG: send DV on router " << router_id<<endl;
    if (DV_map.size() == 0) {
      return;
    }

    cout<< "DEBUG: send DV >>> print DV map" <<endl;
    for (auto iter = DV_map.begin(); iter != DV_map.end(); iter++) {
      cout<<"DEBUG: send DV >>> neighbor id "<<iter->first<<" port id "<<iter->second->port_id<<" cost "<<iter->second->cost<<endl;
    }

    // 32 bit per dv entry in packet payload
    unsigned short packet_size = sizeof(Packet) + DV_map.size() * 4;
    // cout<< "DEBUG: send DV packet_size " <<packet_size<<endl;
    // cout<<"DEBUG: send DV DV_map.size() "<<DV_map.size()<<endl;
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
      unsigned short  *payload_ptr = (unsigned short *)dv_update_packet->payload;
      for (auto iter = DV_map.begin(); iter != DV_map.end(); iter++) {
            // cout<<"DEBUG: send DV neighbor_id "<<iter->first<<endl;

            // destination node id
            *payload_ptr = htons(iter->first);
            payload_ptr++;

            // cost 
            if (iter->second->port_id == i) {
              // poison reverse
              // cout<< "DEBUG: send DV poison reverse"<<endl;
              *payload_ptr = htons(INFINITY_COST);
              payload_ptr++;
            } else {
              *payload_ptr = htons(iter->second->cost);
              payload_ptr++;
            }
      }
      sys->send(i, dv_update_packet, packet_size);
    }
}

void RoutingProtocolImpl::periodic_check_status() {
  // cout<< "DEBUG: check status on router " << router_id<<endl;
  unsigned int curr_time = sys->time();
  bool isDVUpdated = false;

  // check the status of port_status_map
  for (auto iter1 = port_status_map.begin(); iter1 != port_status_map.end(); iter1++) {
      PortStatusEntry *port_status = iter1->second;
      if (port_status->neighbor_id == router_id || port_status->cost == INFINITY_COST) {
        continue;
      }
      // cout<< "DEBUG: check status >>> port_status->last_refreshed_time" << port_status->last_refreshed_time<<endl;
      if (curr_time - port_status->last_refreshed_time >= 15000) {
        cout<< "DEBUG: check status >>> delete out-of-time port entry"<<endl;
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
      // cout<< "DEBUG: check status >>> dv->last_refreshed_time" << dv->last_refreshed_time<<endl;

      // delete out-of-time entry from DV_map
      if (curr_time - dv->last_refreshed_time >= 45000) {
        cout<< "DEBUG: check status >>> delete out-of-time dv entry"<<endl;
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
  data[0] = CHECK_STATUS;
  sys->set_alarm(this, 1000, data);
}

void RoutingProtocolImpl::recv_data(unsigned short port, Packet *packet, unsigned short size) {
    cout<< "DEBUG: recv data on router " << router_id<<endl;
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
  // cout<< "DEBUG: recv PING on router " << router_id<<endl;
   // send PONG
   packet->packet_type = PONG;
   packet->dest_id = packet->source_id;
   packet->source_id = htons(router_id);
  //  cout<< "DEBUG: recv PING ping time " <<ping_time<<endl;
   sys->send(port, packet, size);
}

void RoutingProtocolImpl::recv_PONG(unsigned short port, Packet *packet, unsigned short size) {
  // cout<< "DEBUG: recv Pong on router " << router_id<<endl;
  // update port status entry
  unsigned int ping_time = ntohl(*((uint32_t *)(packet->payload)));
  unsigned int curr_time = sys->time();
  unsigned short neighbor_id = ntohs(packet->source_id);
  PortStatusEntry *port_entry = port_status_map[port];
  unsigned short prev_cost = port_entry->cost;
  unsigned short curr_cost = curr_time - ping_time;
  port_entry->neighbor_id = neighbor_id;
  port_entry->cost = curr_cost;
  port_entry->last_refreshed_time = curr_time;
  // cout<< "DEBUG: recv Pong from port " << port<<endl;
  // cout<< "DEBUG: recv Pong from prev_cost " << prev_cost<<endl;
  // cout<< "DEBUG: recv Pong from curr_cost " << curr_cost<<endl;
  // cout<< "DEBUG: recv Pong with ping_time(ms) " <<ping_time<<endl;
  // cout<< "DEBUG: recv Pong with curr_time(ms) " << curr_time<<endl;

  // update DV map
  bool isDVUpdated = false;
  // case 1: new link 
  if (DV_map.find(neighbor_id) == DV_map.end()) {
      cout<< "DEBUG: recv Pong >>> new link" <<endl;
      // add entry to DV map with healed neighbor
      DV_map[neighbor_id] = new DVEntry(port, port_entry->cost, curr_time);
      isDVUpdated = true;
  } else {
    // case 2: old link with updated cost
    cout<< "DEBUG: recv >>> Pong old link" <<endl;
    auto iter = DV_map.begin();
    while (iter != DV_map.end()) {
        DVEntry *dv_entry = iter->second;
        // case 2.1: the port send the pong packet is the next port of the dv entry
        // update the cost of the entry related with the port
        if (dv_entry->port_id == port) {
          if (iter->first == neighbor_id) {
              dv_entry->cost = port_entry->cost;
              dv_entry->last_refreshed_time = curr_time;
              if (prev_cost != curr_cost) {
                  isDVUpdated = true;
              }
          } else {
              dv_entry->cost += port_entry->cost - prev_cost;
              dv_entry->last_refreshed_time = curr_time;
              if (prev_cost != curr_cost) {
                  isDVUpdated = true;
              }
          }
        } else {
          // case 2.2 the port send the pong packet is not the next port of the dv entry
          // if the cost is smaller between neighbor node and current node, update the cost
          // cout<< "DEBUG: recv >>> Pong old link >>> dv_entry->cost " <<dv_entry->cost<<" port_entry->cost "<<port_entry->cost<<endl;
          if (iter->first == neighbor_id && dv_entry->cost > port_entry->cost) {
              dv_entry->cost = port_entry->cost;
              dv_entry->port_id = port;
              dv_entry->last_refreshed_time = curr_time;
              isDVUpdated = true;
          }
        }
        iter++;
    } 
  }
  // cout<< "DEBUG: recv Pong updated DV map" <<endl;
  // for (auto iter = DV_map.begin(); iter != DV_map.end(); iter++) {
  //    cout<<"DEBUG: recv Pong neighbor id "<<iter->first<<" port id "<<iter->second->port_id<<" cost "<<iter->second->cost<<endl;
  // }
  
  if (isDVUpdated) {
    send_DV();
  }
  delete packet;
}

void RoutingProtocolImpl::recv_DV(unsigned short port, Packet *packet, unsigned short size) {
  cout<< "DEBUG: recv DV packet on router " << router_id<<" from port "<<port<<endl;
  // cout<< "DEBUG: recv DV packet size " << size<<endl;
  // reconstruct recv packet to map with pair <destination id, cost>
  unordered_map<unsigned short, unsigned short> recv_dv_map;
  unsigned short *payload_ptr = (unsigned short *)(packet->payload);
  unsigned short entry_num = (size- sizeof(Packet))/4;
  // cout<< "DEBUG: recv DV packet entry_num " << entry_num<<endl;
  for (int i = 0; i < entry_num; i++) {
      unsigned short dest_id = ntohs(*payload_ptr);
      payload_ptr++;
      unsigned short cost = ntohs(*payload_ptr);
      payload_ptr++;
      recv_dv_map[dest_id] = cost;
  }

  // cout<< "DEBUG: recv DV map" <<endl;
  // for (auto iter = recv_dv_map.begin(); iter != recv_dv_map.end(); iter++) {
  //    cout<<"DEBUG: recv DV neighbor id "<<iter->first<<" cost "<<iter->second<<endl;
  // }

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
      // previous ping failed
      if (port_status_map[port]->cost == INFINITY_COST) {
        continue;
      }
      // case1: new destination node: add new entry to dv map
      if (DV_map.find(recv_dest_id) == DV_map.end()) {
          cout<< "DEBUG: recv DV new entry" <<endl;
          unsigned short total_cost = port_status_map[port]->cost + recv_cost;
          DV_map[recv_dest_id] = new DVEntry(port, total_cost, curr_time);
          isDVUpdated = true;
      } else {
          // case2: exiting destination node: 
          // 1 check cost; if smaller, update entry
          // 2 check port; if the same, update entry
          unsigned short prev_cost = DV_map[recv_dest_id]->cost;
          unsigned short curr_cost = port_status_map[port]->cost + recv_cost;
          if (prev_cost > curr_cost || DV_map[recv_dest_id]->port_id == port) {
            cout<< "DEBUG: recv DV update entry" <<endl;
            DV_map[recv_dest_id]->port_id = port;
            DV_map[recv_dest_id]->cost = curr_cost;
            DV_map[recv_dest_id]->last_refreshed_time = curr_time;
            if (prev_cost != curr_cost) {
              isDVUpdated = true;
            }
          }
      }
  }
  // delete entry with next hop that equals port if destination node isn't found in packet
  auto iter = DV_map.begin();
  while (iter != DV_map.end()) {
    unsigned short dest_id = iter->first;
    DVEntry *dv = iter->second;
    // except the destination of the entry is where the packet comes from 
    if (dest_id == port_status_map[port]->neighbor_id) {
      iter++;
      continue;
    }
    if (recv_dv_map.find(dest_id) == recv_dv_map.end() && dv->port_id == port) {
      cout<< "DEBUG: recv DV delete entry" <<endl;
      delete dv;
      DV_map.erase(iter++);
      isDVUpdated = true;
    } else {
      iter++;
    }
  } 
 
  // // test
  // cout<< "DEBUG: recv DV ->updated DV map" <<endl;
  // for (auto iter = DV_map.begin(); iter != DV_map.end(); iter++) {
  //    cout<<"DEBUG: recv DV neighbor id "<<iter->first<<" port id "<<iter->second->port_id<<" cost "<<iter->second->cost<<endl;
  // }

  if (isDVUpdated) {
    send_DV();
  }
  delete packet;
}

void RoutingProtocolImpl::recv_LS(unsigned short port, Packet *packet, unsigned short size) {
  return;
}

