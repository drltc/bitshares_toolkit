#include <bts/net/peer_connection.hpp>
#include <bts/net/exceptions.hpp>

#ifdef DEFAULT_LOGGER
# undef DEFAULT_LOGGER
#endif
#define DEFAULT_LOGGER "p2p"


namespace bts { namespace net
  {
    peer_connection::peer_connection(peer_connection_delegate* delegate) : 
      _node(delegate),
      _message_connection(this),
      _total_queued_messages_size(0),
      direction(peer_connection_direction::unknown),
      is_firewalled(firewalled_state::unknown),
      our_state(our_connection_state::disconnected),
      they_have_requested_close(false),
      their_state(their_connection_state::disconnected),
      we_have_requested_close(false),
      negotiation_status(connection_negotiation_status::disconnected),
      number_of_unfetched_item_ids(0),
      peer_needs_sync_items_from_us(true),
      we_need_sync_items_from_peer(true),
      last_block_number_delegate_has_seen(0),
      inhibit_fetching_sync_blocks(false),
      transaction_fetching_inhibited_until(fc::time_point::min())
    {
    }

    peer_connection_ptr peer_connection::make_shared(peer_connection_delegate* delegate)
    {
      // The lifetime of peer_connection objects is managed by shared_ptrs in node.  The peer_connection
      // is responsible for notifying the node when it should be deleted, and the process of deleting it
      // cleans up the peer connection's asynchronous tasks which are responsible for notifying the node
      // when it should be deleted.
      // To ease this vicious cycle, we slightly delay the execution of the destructor until the
      // current task yields.  In the (not uncommon) case where it is the task executing 
      // connect_to or read_loop, this allows the task to finish before the destructor is forced
      // to cancel it.
      return peer_connection_ptr(new peer_connection(delegate));
      //, [](peer_connection* peer_to_delete){ fc::async([peer_to_delete](){delete peer_to_delete;}); });
    }

    void peer_connection::destroy()
    {
      try 
      {
        close_connection();
      } 
      catch ( ... ) 
      {
      }

      try 
      { 
        _send_queued_messages_done.cancel_and_wait(); 
      } 
      catch( const fc::exception& e )
      {
        wlog("Unexpected exception from peer_connection's send_queued_messages_task : ${e}", ("e", e));
      }
      catch( ... )
      {
        wlog("Unexpected exception from peer_connection's send_queued_messages_task");
      }
      
      try 
      { 
        accept_or_connect_task_done.cancel_and_wait(); 
      } 
      catch( const fc::exception& e )
      {
        wlog("Unexpected exception from peer_connection's accept_or_connect_task : ${e}", ("e", e));
      }
      catch( ... )
      {
        wlog("Unexpected exception from peer_connection's accept_or_connect_task");
      }
    }

    peer_connection::~peer_connection()
    {
      destroy();
    }

    fc::tcp_socket& peer_connection::get_socket()
    {
      return _message_connection.get_socket();
    }

    void peer_connection::accept_connection()
    {
      try
      {
        assert( our_state == our_connection_state::disconnected &&
                their_state == their_connection_state::disconnected );
        direction = peer_connection_direction::inbound;
        negotiation_status = connection_negotiation_status::accepting;
        _message_connection.accept();           // perform key exchange
        negotiation_status = connection_negotiation_status::accepted;
        _remote_endpoint = _message_connection.get_socket().remote_endpoint();

        // firewall-detecting info is pretty useless for inbound connections, but initialize 
        // it the best we can
        fc::ip::endpoint local_endpoint = _message_connection.get_socket().local_endpoint();
        inbound_address = local_endpoint.get_address();
        inbound_port = local_endpoint.port();
        outbound_port = inbound_port;

        their_state = their_connection_state::just_connected;
        our_state = our_connection_state::just_connected;
        ilog( "established inbound connection from ${remote_endpoint}, sending hello", ("remote_endpoint", _message_connection.get_socket().remote_endpoint() ) );
      }
      catch ( const fc::exception& e )
      {
        wlog( "error accepting connection ${e}", ("e", e.to_detail_string() ) );
        throw;
      }
    }

    void peer_connection::connect_to( const fc::ip::endpoint& remote_endpoint, fc::optional<fc::ip::endpoint> local_endpoint )
    {
      try
      {
        assert( our_state == our_connection_state::disconnected && 
                their_state == their_connection_state::disconnected );
        direction = peer_connection_direction::outbound;

        _remote_endpoint = remote_endpoint;
        if( local_endpoint )
        {
          // the caller wants us to bind the local side of this socket to a specific ip/port
          // This depends on the ip/port being unused, and on being able to set the 
          // SO_REUSEADDR/SO_REUSEPORT flags, and either of these might fail, so we need to 
          // detect if this fails.
          try
          {
            _message_connection.bind( *local_endpoint );
          }
          catch ( const fc::exception& except )
          {
            wlog( "Failed to bind to desired local endpoint ${endpoint}, will connect using an OS-selected endpoint: ${except}", ("endpoint", *local_endpoint )("except", except ) );
          }
        }
        negotiation_status = connection_negotiation_status::connecting;
        _message_connection.connect_to( remote_endpoint );
        negotiation_status = connection_negotiation_status::connected;
        their_state = their_connection_state::just_connected;
        our_state = our_connection_state::just_connected;
        ilog( "established outbound connection to ${remote_endpoint}", ("remote_endpoint", remote_endpoint ) );
      }
      catch ( fc::exception& e )
      {
        elog( "fatal: error connecting to peer ${remote_endpoint}: ${e}", ("remote_endpoint", remote_endpoint )("e", e.to_detail_string() ) );
        throw;
      }
    } // connect_to()

    void peer_connection::on_message( message_oriented_connection* originating_connection, const message& received_message )
    {
      _node->on_message( this, received_message );
    }

    void peer_connection::on_connection_closed( message_oriented_connection* originating_connection )
    {
      negotiation_status = connection_negotiation_status::closed;
      _node->on_connection_closed( this );
    }

    void peer_connection::send_queued_messages_task()
    {
      while (!_queued_messages.empty())
      {
        _queued_messages.front().transmission_start_time = fc::time_point::now();
        try
        {
          _message_connection.send_message(_queued_messages.front().message_to_send);
        }
        catch (const fc::exception& send_error)
        {
          elog("Error sending message: ${exception}.  Closing connection.", ("exception", send_error));
          try
          {
            close_connection();
          }
          catch (const fc::exception& close_error)
          {
            elog("Caught error while closing connection: ${exception}", ("exception", close_error));
          }
          return;
        }
        _queued_messages.front().transmission_finish_time = fc::time_point::now();
        _total_queued_messages_size -= _queued_messages.front().message_to_send.size;
        _queued_messages.pop();
      }
    }

    void peer_connection::send_message( const message& message_to_send )
    {
      _queued_messages.emplace(queued_message(message_to_send));
      _total_queued_messages_size += message_to_send.size;
      if (_total_queued_messages_size > BTS_NET_MAXIMUM_QUEUED_MESSAGES_IN_BYTES)
      {
        elog("send queue exceeded maximum size of ${max} bytes (current size ${current} bytes)",
             ("max", BTS_NET_MAXIMUM_QUEUED_MESSAGES_IN_BYTES)("current", _total_queued_messages_size));
        try
        {
          close_connection();
        }
        catch (const fc::exception& e)
        {
          elog("Caught error while closing connection: ${exception}", ("exception", e));
        }
        return;
      }

      if( _send_queued_messages_done.valid() && _send_queued_messages_done.canceled() ) 
        FC_THROW_EXCEPTION(fc::exception, "Attempting to send a message on a connection that is being shut down");

      if (!_send_queued_messages_done.valid() || _send_queued_messages_done.ready())
           _send_queued_messages_done = fc::async([this](){ send_queued_messages_task(); }, "send_queued_messages_task");
    }

    void peer_connection::close_connection()
    {
      negotiation_status = connection_negotiation_status::closing;
      if (connection_terminated_time != fc::time_point::min())
        connection_terminated_time = fc::time_point::now();
      _message_connection.close_connection();
    }

    void peer_connection::destroy_connection()
    {
      negotiation_status = connection_negotiation_status::closing;
      destroy();
    }

    uint64_t peer_connection::get_total_bytes_sent() const
    {
      return _message_connection.get_total_bytes_sent();
    }

    uint64_t peer_connection::get_total_bytes_received() const
    {
      return _message_connection.get_total_bytes_received();
    }

    fc::time_point peer_connection::get_last_message_sent_time() const
    {
      return _message_connection.get_last_message_sent_time();
    }

    fc::time_point peer_connection::get_last_message_received_time() const
    {
      return _message_connection.get_last_message_received_time();
    }

    fc::optional<fc::ip::endpoint> peer_connection::get_remote_endpoint()
    {
      return _remote_endpoint;
    }
    fc::ip::endpoint peer_connection::get_local_endpoint()
    {
      return _message_connection.get_socket().local_endpoint();
    }

    void peer_connection::set_remote_endpoint( fc::optional<fc::ip::endpoint> new_remote_endpoint )
    {
      _remote_endpoint = new_remote_endpoint;
    }

    bool peer_connection::busy() 
    { 
      return !items_requested_from_peer.empty() || !sync_items_requested_from_peer.empty() || item_ids_requested_from_peer;
    }

    bool peer_connection::idle()
    {
      return !busy();
    }

    bool peer_connection::is_transaction_fetching_inhibited() const
    {
      return transaction_fetching_inhibited_until > fc::time_point::now();
    }

    fc::sha512 peer_connection::get_shared_secret() const
    {
      return _message_connection.get_shared_secret();
    }

    void peer_connection::clear_old_inventory_advertised_to_peer()
    {
      fc::time_point_sec oldest_inventory_to_keep(fc::time_point::now() - fc::minutes(BTS_NET_MAX_INVENTORY_SIZE_IN_MINUTES));
      auto oldest_inventory_to_keep_iter = inventory_advertised_to_peer.get<timestamp_index>().lower_bound(oldest_inventory_to_keep);
      auto begin_iter = inventory_advertised_to_peer.get<timestamp_index>().begin();
      //unsigned number_of_elements_to_discard = std::distance(begin_iter, oldest_inventory_to_keep_iter);
      inventory_advertised_to_peer.get<timestamp_index>().erase(begin_iter, oldest_inventory_to_keep_iter);
    }
    // we have a higher limit for blocks than transactions so we will still fetch blocks even when transactions are throttled
    bool peer_connection::is_inventory_advertised_to_us_list_full_for_transactions() const
    {
      return inventory_peer_advertised_to_us.size() > BTS_NET_MAX_INVENTORY_SIZE_IN_MINUTES * BTS_BLOCKCHAIN_MAX_TRX_PER_SECOND * 60;
    }
    bool peer_connection::is_inventory_advertised_to_us_list_full() const
    {
      return inventory_peer_advertised_to_us.size() > BTS_NET_MAX_INVENTORY_SIZE_IN_MINUTES * BTS_BLOCKCHAIN_MAX_TRX_PER_SECOND * 60 + 5;
    }
} } // end namespace bts::net
