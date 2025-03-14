#include <cstdint>
#include <iostream>

#include "address.hh"
#include "arp_message.hh"
#include "debug.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "exception.hh"
#include "helpers.hh"
#include "ipv4_datagram.hh"
#include "network_interface.hh"

using namespace std;

ARPMessage NetworkInterface::make_arp( const uint16_t opcode,
                                       const EthernetAddress target_ethernet_address,
                                       const uint32_t target_ip_address )
{
  ARPMessage arp;
  arp.opcode = opcode;
  arp.sender_ethernet_address = ethernet_address_;
  arp.sender_ip_address = ip_address_.ipv4_numeric();
  arp.target_ethernet_address = target_ethernet_address;
  arp.target_ip_address = target_ip_address;
  return arp;
}

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
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  uint32_t next_hop_ip = next_hop.Address::ipv4_numeric();

  auto it = arp_cache_.find( next_hop_ip );
  if ( it != arp_cache_.end()) {
    // 目的 MAC 地址已知
    EthernetFrame frame;
    frame.header = { it->second.first, ethernet_address_, EthernetHeader::TYPE_IPv4 };
    frame.payload = serialize( dgram );

    transmit( frame );
    return;
  }

  // 目的 MAC 地址未知
  waiting_frames_[next_hop_ip].emplace( std::move(dgram) );
  if ( !arp_requests_.contains( next_hop_ip ) ) {
    ARPMessage arp_req = make_arp( ARPMessage::OPCODE_REQUEST, {}, next_hop_ip );
    EthernetFrame frame;
    frame.header = { ETHERNET_BROADCAST, ethernet_address_, EthernetHeader::TYPE_ARP };
    frame.payload = serialize( arp_req );

    arp_requests_[next_hop_ip] = current_time_;

    transmit( frame );
  }
  return;
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( EthernetFrame frame )
{
  EthernetAddress dst_mac = frame.header.dst;
  auto frame_type = frame.header.type;

  if ( dst_mac != ethernet_address_ && dst_mac != ETHERNET_BROADCAST ) {
    return;
  }

  if ( frame_type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram dgram;
    if ( parse( dgram, frame.payload ) ) {
      datagrams_received_.emplace( std::move( dgram ) );
    }
  } else if ( frame_type == EthernetHeader::TYPE_ARP ) {
    ARPMessage arp_msg;
    if ( parse( arp_msg, frame.payload ) ) {
      arp_cache_[arp_msg.sender_ip_address] = { arp_msg.sender_ethernet_address, current_time_ };
      
      if ( arp_msg.opcode == ARPMessage::OPCODE_REPLY ) {
        // 发送所有等待的数据报
        auto it = waiting_frames_.find( arp_msg.sender_ip_address );
        if ( it != waiting_frames_.end() ) {
          while ( !it->second.empty() ) {
            InternetDatagram dgram = it->second.front();
            it->second.pop();

            EthernetFrame send_frame;
            send_frame.header = { arp_msg.sender_ethernet_address, ethernet_address_, EthernetHeader::TYPE_IPv4 };
            send_frame.payload = serialize( dgram );
            transmit( send_frame );
          }
          waiting_frames_.erase( it ); // 清除已发送的数据报
        }
      }

      if ( arp_msg.opcode == ARPMessage::OPCODE_REQUEST
           && arp_msg.target_ip_address == ip_address_.ipv4_numeric() ) {
        ARPMessage arp_reply
          = make_arp( ARPMessage::OPCODE_REPLY, arp_msg.sender_ethernet_address, arp_msg.sender_ip_address );
        EthernetFrame reply_frame;
        reply_frame.header = { arp_msg.sender_ethernet_address, ethernet_address_, EthernetHeader::TYPE_ARP };
        reply_frame.payload = serialize( arp_reply );
        transmit( reply_frame );
      }
    }
  }
  return;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  current_time_ += ms_since_last_tick;

  // 移除超过 30 秒的 ARP 缓存
  for ( auto it = arp_cache_.begin(); it != arp_cache_.end(); ) {
    if ( current_time_ - it->second.second > 30000 ) {
      it = arp_cache_.erase( it );
    } else {
      ++it;
    }
  }

  // 移除 5 秒以上的 ARP 请求，并清理等待的数据报
  for ( auto it = arp_requests_.begin(); it != arp_requests_.end(); ) {
    if ( current_time_ - it->second > 5000 ) {
      waiting_frames_.erase( it->first );  // 同时删除等待的数据报
      it = arp_requests_.erase( it );
    } else {
      ++it;
    }
  }
}
