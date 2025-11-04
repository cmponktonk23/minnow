#include "router.hh"
#include "debug.hh"

#include <iostream>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  route_table_.emplace_back( route_prefix, prefix_length, next_hop, interface_num );
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  for ( auto& _interface : interfaces_ ) {
    auto& queue = _interface->datagrams_received();
    while ( !queue.empty() ) {
      auto dgram = queue.front();
      queue.pop();
      uint32_t dst_ip = dgram.header.dst;
      Rule matched_rule;
      bool first = true;
      matched_rule.prefix_length = 0;
      for ( auto& rule : route_table_ ) {
        if ( match( rule, dst_ip ) && ( rule.prefix_length > matched_rule.prefix_length || first ) ) {
          matched_rule = rule;
          first = false;
        }
      }
      if ( ( matched_rule.prefix_length > 0 || !first ) && dgram.header.ttl > 1 ) {
        auto next_hop = matched_rule.next_hop.has_value() ? matched_rule.next_hop.value() : Address::from_ipv4_numeric( dst_ip );        
        auto matched_interface = interface( matched_rule.interface_num );
        --dgram.header.ttl;
        matched_interface->send_datagram( dgram, next_hop );
      }
    }
  }
}

auto Router::match( const Rule& rule, const uint32_t ip ) -> bool 
{
  if ( rule.prefix_length == 0 ) return true;
  uint8_t shift = 32 - rule.prefix_length;
  return (rule.route_prefix >> shift) == (ip >> shift);
}