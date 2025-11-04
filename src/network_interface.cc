#include <iostream>

#include "arp_message.hh"
#include "debug.hh"
#include "ethernet_frame.hh"
#include "exception.hh"
#include "helpers.hh"
#include "network_interface.hh"
#include "parser.hh"


using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( InternetDatagram dgram, const Address& next_hop )
{
  uint32_t dst_ip = next_hop.ipv4_numeric();

  // Send datagram if mapping exists.
  auto it = ip_to_ethernet_.find(dst_ip);
  if ( it != ip_to_ethernet_.end() ) {
    send_datagram_frame( dgram, it->second.ethernet_address );
    return;
  }

  // Send ARP
  auto it_d = ip_to_dgrams_.find(dst_ip);
  // Re-send ARP only if the same IP has been sent 5 second ago
  if ( it_d == ip_to_dgrams_.end() || it_d->second.ts == -1 ) {
    auto arp_req = make_arp(ARPMessage::OPCODE_REQUEST, ethernet_address_, ip_address_.ipv4_numeric(), {}, dst_ip);
    send_arp_frame( arp_req );
  }

  // Store the datagram no matter re-send ARP or not.
  ip_to_dgrams_[dst_ip].dgramq.emplace_back(0, std::move( dgram ));
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( EthernetFrame frame )
{
  const auto& header = frame.header;
  
  // Receive an IP datagram
  if ( header.type == EthernetHeader::TYPE_IPv4 ) {
    // Drop the datagram if its dst is not current host.
    if ( header.dst == ethernet_address_ ) {
      InternetDatagram dgram;
      Parser p{ frame.payload };
      dgram.parse( p );
      datagrams_received_.push( std::move(dgram) );
    }
  } else if ( header.type == EthernetHeader::TYPE_ARP ) {
    // Receive an ARP msg.
    ARPMessage arp;
    Parser p{ frame.payload };
    arp.parse( p );

    // Learn mappings from both requests and replies
    ip_to_ethernet_[arp.sender_ip_address] = { 0, arp.sender_ethernet_address };

    /**
     * Bug: Everytime a host learn a mapping, check to send the pending datagrams. (Even if it's not the target host.)
     * ARP reply msg is also covered here!
     */
    auto it = ip_to_dgrams_.find(arp.sender_ip_address);
    if ( it != ip_to_dgrams_.end() ) {
      for (auto &ts_dgram : it->second.dgramq) {
        send_datagram_frame( ts_dgram.dgram, arp.sender_ethernet_address );
      }
      ip_to_dgrams_.erase(it);
    }
    
    auto ip = ip_address_.ipv4_numeric();
    // Reply to an ARP request only if current host is the target.
    if ( arp.opcode == ARPMessage::OPCODE_REQUEST && arp.target_ip_address == ip ) {
      /**
        * Bug: Sender is always current host and target is always the destination host for both request and reply ARP msg.
        */
      auto arp_reply = make_arp( ARPMessage::OPCODE_REPLY, ethernet_address_, ip, arp.sender_ethernet_address, arp.sender_ip_address );
      send_arp_frame( arp_reply, arp.sender_ethernet_address );
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  // Expire ip - ethernet mappings
  for ( auto it = ip_to_ethernet_.begin(); it != ip_to_ethernet_.end(); ) {
    auto& ts_ethernet = it->second;
    ts_ethernet.ts += ms_since_last_tick;
    if ( ts_ethernet.ts >= NetworkInterface::MAPPING_CACHE_DURATION ) {
      it = ip_to_ethernet_.erase(it);
    } else {
      it = next(it);
    }
  }

  // Update ARP re-send timeout
  for ( auto it = ip_to_dgrams_.begin(); it != ip_to_dgrams_.end(); it = next(it) ) {
    auto& ts_dgramq = it->second;
    if ( ts_dgramq.ts != -1 ) {
      ts_dgramq.ts += ms_since_last_tick;
      if ( ts_dgramq.ts >= NetworkInterface::ARP_RESEND_TIMEOUT ) {
        ts_dgramq.ts = -1;
      }
      /**
       * Bug: Drop pending datagrams after enqueue for 5s.
       */
      auto &dgramq = ts_dgramq.dgramq;
      for ( size_t i = 0; i < dgramq.size(); ++i ) {
        dgramq[i].ts += ms_since_last_tick;
        if ( dgramq[i].ts >= NetworkInterface::ARP_RESEND_TIMEOUT ) {
          swap(dgramq[i], dgramq.back());
          dgramq.pop_back();
        }
      }
    }
  }
}

auto NetworkInterface::make_arp(
  uint16_t opcode, 
  const EthernetAddress& sender_ethernet_address,
  const uint32_t sender_ip_address,
  const EthernetAddress& target_ethernet_address,
  const uint32_t target_ip_address
) -> ARPMessage {
  ARPMessage arp;
  arp.opcode = opcode;
  arp.sender_ethernet_address = sender_ethernet_address;
  arp.sender_ip_address = sender_ip_address;
  arp.target_ethernet_address = target_ethernet_address;
  arp.target_ip_address = target_ip_address;
  return arp;
}

void NetworkInterface::send_datagram_frame( const InternetDatagram& dgram, const EthernetAddress& dst_ethernet_address )
{
  EthernetHeader header{ dst_ethernet_address, ethernet_address_, EthernetHeader::TYPE_IPv4 };
  
  // IP datagram is the payload of the ethernet frame
  Serializer s;
  dgram.serialize(s);
  auto payload = s.finish();

  auto frame = EthernetFrame{ header, payload };
  transmit( frame );
}

void NetworkInterface::send_arp_frame( const ARPMessage& arp, const EthernetAddress& dst_ethernet_address )
{
  EthernetHeader header{ dst_ethernet_address, ethernet_address_, EthernetHeader::TYPE_ARP };

  Serializer s;
  arp.serialize(s);
  auto payload = s.finish();

  auto frame = EthernetFrame{ header, payload };
  transmit( frame );
}
