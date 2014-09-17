#pragma once

#include <bts/blockchain/types.hpp>
#include <bts/blockchain/asset.hpp>

#include <fc/time.hpp>

namespace bts { namespace blockchain {

   struct account_meta_info
   {
      fc::unsigned_int type;
      vector<char>     data;
   };

   struct delegate_stats
   {
      delegate_stats( uint8_t pay_rate = 100 )
      :votes_for(0),
       blocks_produced(0),
       blocks_missed(0),
       last_block_num_produced(0),
       pay_rate(pay_rate),
       pay_balance(0){}

      share_type                     votes_for;
      uint32_t                       blocks_produced;
      uint32_t                       blocks_missed;
      secret_hash_type               next_secret_hash;
      uint32_t                       last_block_num_produced;
      uint8_t                        pay_rate;

      /**
       *  Delegate pay is held in escrow and may be siezed 
       *  and returned to the shareholders if they are fired
       *  for provable cause.
       */
      share_type                     pay_balance;
   };
   typedef fc::optional<delegate_stats> odelegate_stats;

   struct account_record
   {
      account_record()
      :id(0),points(0){}

      bool              is_null()const;
      account_record    make_null()const;

      share_type        delegate_pay_balance()const;
      bool              is_delegate()const;
      void              adjust_votes_for( share_type delta );
      share_type        net_votes()const;
      bool              is_retracted()const;
      address           active_address()const;
      void              set_active_key( const time_point_sec& now, const public_key_type& new_key );
      public_key_type   active_key()const;
      uint8_t           delegate_pay_rate()const;

      account_id_type                        id = 0;
      std::string                            name;
      fc::variant                            public_data;
      public_key_type                        owner_key;
      map<time_point_sec, public_key_type>   active_key_history;
      fc::time_point_sec                     registration_date;
      fc::time_point_sec                     last_update;
      optional<delegate_stats>               delegate_info;
      optional<account_meta_info>            meta_data;
      share_type                             points;
   };
   typedef fc::optional<account_record> oaccount_record;

   struct burn_record_key
   {
      account_id_type     account_id;
      transaction_id_type transaction_id;
      friend bool operator < ( const burn_record_key& a, const burn_record_key& b )
      {
         return std::tie( a.account_id, a.transaction_id) < std::tie( b.account_id, b.transaction_id);
      }
      friend bool operator == ( const burn_record_key& a, const burn_record_key& b )
      {
         return std::tie( a.account_id, a.transaction_id) == std::tie( b.account_id, b.transaction_id);
      }
   };

   struct burn_record_value
   {
      bool is_null()const { return amount.amount == 0; }
      asset                    amount;
      string                   message;
      optional<signature_type> signer;
   };

   struct burn_record : public burn_record_key, public burn_record_value
   {
      burn_record(){}
      burn_record( const burn_record_key& k, const burn_record_value& v = burn_record_value())
      :burn_record_key(k),burn_record_value(v){}
   };
   typedef optional<burn_record> oburn_record;

} } // bts::blockchain

FC_REFLECT( bts::blockchain::account_meta_info, (type)(data) )

FC_REFLECT( bts::blockchain::account_record,
            (id)(name)(public_data)(owner_key)(active_key_history)(registration_date)(last_update)(delegate_info)(meta_data)(points) )
FC_REFLECT( bts::blockchain::delegate_stats, 
            (votes_for)(blocks_produced)(blocks_missed)(pay_rate)(pay_balance)(next_secret_hash)(last_block_num_produced) )
FC_REFLECT( bts::blockchain::burn_record_key,   (account_id)(transaction_id) )
FC_REFLECT( bts::blockchain::burn_record_value, (amount)(message)(signer) )
FC_REFLECT_DERIVED( bts::blockchain::burn_record, (bts::blockchain::burn_record_key)(bts::blockchain::burn_record_value), BOOST_PP_SEQ_NIL )
