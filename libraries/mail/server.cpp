#include <bts/mail/server.hpp>
#include <bts/db/level_map.hpp>
#include <bts/db/level_pod_map.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/time.hpp>

namespace bts { namespace mail {

struct mail_index
{
   bts::blockchain::address    owner;
   fc::time_point              received;
};

bool operator < ( const mail_index& a, const mail_index& b )
{
   if( a.owner < b.owner ) return true;
   if( a.owner == b.owner ) return a.received < b.received;
   return false;
}
bool operator == ( const mail_index& a, const mail_index& b )
{
   return a.owner == b.owner && a.received == b.received;
}

}  } // bts::mail

namespace bts { namespace mail {
   using std::vector;
   using std::pair;

   namespace detail
   {
      class server_impl
      {
          public:
            server_impl( const fc::path& data_dir )
            {
               _mail_inventory_db.open( data_dir / "mail_inventory_db" );
               _mail_data_db.open( data_dir / "mail_data_db" );
            }

            ~server_impl()
            {
               try {
                  _mail_inventory_db.close();
                  _mail_data_db.close();
               } 
               catch ( const fc::exception& e )
               {
                  wlog( "error closing mail database: ${e}", ("e",e.to_detail_string()) );
               }
            }

            void store( const bts::blockchain::address& owner, const message& msg )
            { try {
               FC_ASSERT( msg.data.size() > 0 );

               auto inventory_id = msg.id();

               /**
                *  Prevent the same message from going to multiple accounts.
                */
               FC_ASSERT( !_mail_data_db.fetch_optional(inventory_id) );

               _mail_inventory_db.store( mail_index{owner,fc::time_point::now()}, inventory_id );
               _mail_data_db.store( inventory_id, msg );
            } FC_CAPTURE_AND_RETHROW( (owner)(msg) ) }

            inventory_type fetch_inventory( const bts::blockchain::address& owner, 
                                            const fc::time_point& start, 
                                            uint32_t limit = BTS_MAIL_INVENTORY_FETCH_LIMIT )
            { try {
               if( limit > BTS_MAIL_INVENTORY_FETCH_LIMIT ) 
                  limit = BTS_MAIL_INVENTORY_FETCH_LIMIT;

               inventory_type result;
               result.reserve( limit );

               auto itr = _mail_inventory_db.lower_bound( mail_index{ owner, start } );
               while( itr.valid() && result.size() < limit )
               {
                  const auto key = itr.key();
                  if( key.owner == owner )
                     result.push_back( pair<fc::time_point,message_id_type>(key.received,itr.value()) );
                  else
                     return result;
                  ++itr;
               }
               return result;
            } FC_CAPTURE_AND_RETHROW( (owner)(start)(limit) ) }

            message fetch_message( const message_id_type& inventory_id )
            { try {
               return _mail_data_db.fetch( inventory_id );
            } FC_CAPTURE_AND_RETHROW( (inventory_id) ) }

         private:
            bts::db::level_pod_map< mail_index, message_id_type >   _mail_inventory_db;
            bts::db::level_map< message_id_type, message >          _mail_data_db;
      };

   } // namespace detail


   server::server(){}
   server::~server(){}
   
   void server::open( const fc::path& datadir )
   {
      my.reset( new detail::server_impl( datadir ) );
   }
   void server::close()
   {
      my.reset();
   }

   void server::store( const bts::blockchain::address& owner, const message& msg )
   {
      my->store(owner,msg);
   }

   inventory_type server::fetch_inventory( const bts::blockchain::address& owner, 
                                           const fc::time_point& start,
                                           uint32_t limit )const
   {
      return my->fetch_inventory( owner, start, limit );
   }
   message server::fetch_message( const message_id_type& inventory_id )const
   {
      return my->fetch_message( inventory_id );
   }

} } // bts::mail

FC_REFLECT( bts::mail::mail_index, (owner)(received) );
