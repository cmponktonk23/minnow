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

  // Send IP
  auto it = ip_to_ethernet_.find(dst_ip);
  if ( it != ip_to_ethernet_.end() ) {
    send_datagram_frame( dgram, it->second.second );
    return;
  }

  // Send ARP
  auto it_d = ip_to_dgrams_.find(dst_ip);
  if ( it_d == ip_to_dgrams_.end() || it_d->second.first == -1 ) {
    ARPMessage arp_req;
    arp_req.opcode = ARPMessage::OPCODE_REQUEST;
    arp_req.sender_ethernet_address = ethernet_address_;
    arp_req.sender_ip_address = ip_address_.ipv4_numeric(),
    arp_req.target_ip_address = dst_ip;
    send_arp_frame( arp_req );
  }

  // Store the datagram
  ip_to_dgrams_[dst_ip].second.push_back( {0, std::move( dgram )} );
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( EthernetFrame frame )
{
  auto header = frame.header;
  
  if ( header.type == EthernetHeader::TYPE_IPv4 ) {
    if ( header.dst == ethernet_address_ ) {
      InternetDatagram dgram;
      Parser p{ frame.payload };
      dgram.parse( p );
      datagrams_received_.push( std::move(dgram) );
    }
  } else if ( header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage arp;
    Parser p{ frame.payload };
    arp.parse( p );

    // Learn mappings from both requests and replies
    ip_to_ethernet_[arp.sender_ip_address] = { 0, arp.sender_ethernet_address };

    auto it = ip_to_dgrams_.find(arp.sender_ip_address);
    if ( it != ip_to_dgrams_.end() ) {
      for (auto &[_, dgram] : it->second.second) {
        send_datagram_frame( dgram, arp.sender_ethernet_address );
      }
      ip_to_dgrams_.erase(it);
    }
    
    auto ip = ip_address_.ipv4_numeric();
    if ( arp.opcode == ARPMessage::OPCODE_REQUEST && arp.target_ip_address == ip ) {
      ARPMessage arp_reply;
      arp_reply.opcode = ARPMessage::OPCODE_REPLY;
      arp_reply.sender_ethernet_address = ethernet_address_;
      arp_reply.sender_ip_address = ip;
      arp_reply.target_ethernet_address = arp.sender_ethernet_address;
      arp_reply.target_ip_address = arp.sender_ip_address;
      send_arp_frame( arp_reply, arp.sender_ethernet_address );
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  // Expire ip - ethernet mappings
  for ( auto it = ip_to_ethernet_.begin(); it != ip_to_ethernet_.end(); ) {
    it->second.first += ms_since_last_tick;
    if ( it->second.first >= 30000 ) {
      it = ip_to_ethernet_.erase(it);
    } else {
      it = next(it);
    }
  }

  // ARP resend timeout
  for ( auto it = ip_to_dgrams_.begin(); it != ip_to_dgrams_.end(); it = next(it) ) {
    if ( it->second.first != -1 ) {
      it->second.first += ms_since_last_tick;
      if ( it->second.first >= 5000 ) {
        it->second.first = -1;
      }
      auto &pairs = it->second.second;
      for ( size_t i = 0; i < pairs.size(); ++i ) {
        pairs[i].first += ms_since_last_tick;
        if ( pairs[i].first >= 5000 ) {
          swap(pairs[i], pairs[pairs.size() - 1]);
          pairs.pop_back();
        }
      }
    }
  }
}

void NetworkInterface::send_datagram_frame( const InternetDatagram& dgram, const EthernetAddress& dst_ethernet_address )
{
  EthernetHeader header{ dst_ethernet_address, ethernet_address_, EthernetHeader::TYPE_IPv4 };
  
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
