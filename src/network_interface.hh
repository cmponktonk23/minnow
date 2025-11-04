#pragma once

#include "address.hh"
#include "ethernet_frame.hh"
#include "ipv4_datagram.hh"

#include <memory>
#include <queue>
#include <unordered_map>

// A "network interface" that connects IP (the internet layer, or network layer)
// with Ethernet (the network access layer, or link layer).

// This module is the lowest layer of a TCP/IP stack
// (connecting IP with the lower-layer network protocol,
// e.g. Ethernet). But the same module is also used repeatedly
// as part of a router: a router generally has many network
// interfaces, and the router's job is to route Internet datagrams
// between the different interfaces.

// The network interface translates datagrams (coming from the
// "customer," e.g. a TCP/IP stack or router) into Ethernet
// frames. To fill in the Ethernet destination address, it looks up
// the Ethernet address of the next IP hop of each datagram, making
// requests with the [Address Resolution Protocol](\ref rfc::rfc826).
// In the opposite direction, the network interface accepts Ethernet
// frames, checks if they are intended for it, and if so, processes
// the the payload depending on its type. If it's an IPv4 datagram,
// the network interface passes it up the stack. If it's an ARP
// request or reply, the network interface processes the frame
// and learns or replies as necessary.
class NetworkInterface
{
public:
  // An abstraction for the physical output port where the NetworkInterface sends Ethernet frames
  class OutputPort
  {
  public:
    virtual void transmit( const NetworkInterface& sender, const EthernetFrame& frame ) = 0;
    virtual ~OutputPort() = default;
  };

  // Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer)
  // addresses
  NetworkInterface( std::string_view name,
                    std::shared_ptr<OutputPort> port,
                    const EthernetAddress& ethernet_address,
                    const Address& ip_address );

  // Sends an Internet datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination
  // address). Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address for the next
  // hop. Sending is accomplished by calling `transmit()` (a member variable) on the frame.
  void send_datagram( InternetDatagram dgram, const Address& next_hop );

  // Receives an Ethernet frame and responds appropriately.
  // If type is IPv4, pushes the datagram to the datagrams_in queue.
  // If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
  // If type is ARP reply, learn a mapping from the "sender" fields.
  void recv_frame( EthernetFrame frame );

  // Called periodically when time elapses
  void tick( size_t ms_since_last_tick );

  // Accessors
  const std::string& name() const { return name_; }
  const OutputPort& output() const { return *port_; }
  OutputPort& output() { return *port_; }
  std::queue<InternetDatagram>& datagrams_received() { return datagrams_received_; }

private:
  void send_datagram_frame( const InternetDatagram& dgram, const EthernetAddress& dst_ethernet_address );
  void send_arp_frame( const ARPMessage& arp, const EthernetAddress& dst_ethernet_address = ETHERNET_BROADCAST );
  auto make_arp(uint16_t opcode, 
    const EthernetAddress& sender_ethernet_address, const uint32_t sender_ip_address, 
    const EthernetAddress& target_ethernet_address, const uint32_t target_ip_address) -> ARPMessage;

  struct DatagramWithTimeout {
    int64_t ts{};
    InternetDatagram dgram{};
  };

  struct EthernetAddressWithTimeout {
    int64_t ts{};
    EthernetAddress ethernet_address{};
  };

  struct DatagramQueueWithTimeout {
    int64_t ts{};
    std::vector<DatagramWithTimeout> dgramq{};
  };

  static uint16_t MAPPING_CACHE_DURATION = 30000;
  static uint16_t ARP_RESEND_TIMEOUT = 5000;

  // Human-readable name of the interface
  std::string name_;

  // The physical output port (+ a helper function `transmit` that uses it to send an Ethernet frame)
  std::shared_ptr<OutputPort> port_;
  void transmit( const EthernetFrame& frame ) const { port_->transmit( *this, frame ); }

  // Ethernet (known as hardware, network-access-layer, or link-layer) address of the interface
  EthernetAddress ethernet_address_;

  // IP (known as internet-layer or network-layer) address of the interface
  Address ip_address_;

  // Datagrams that have been received
  std::queue<InternetDatagram> datagrams_received_ {};

  /**
   * Only cache each mapping for 30s.
   */
  std::unordered_map<uint32_t, EthernetAddressWithTimeout> ip_to_ethernet_{};

  /**
   * The first timestamp is used to record the timeout (5s) to re-send the APR.
   * The second timestamp is used to record the timeout (5s) to drop the pending datagram.
   */
  std::unordered_map<uint32_t, DatagramQueueWithTimeout> ip_to_dgrams_{};
};
