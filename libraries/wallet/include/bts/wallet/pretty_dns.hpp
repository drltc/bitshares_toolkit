#pragma once

#include <bts/blockchain/types.hpp>
#include <bts/blockchain/dns_record.hpp>

#include <fc/time.hpp>

namespace bts { namespace wallet {
 
    struct pretty_domain_auction_summary
    {
        string                                domain_name;
        double                                last_bid_price;
        double                                next_required_bid_price;
        fc::time_point_sec                    last_bid_time;
        uint32_t                              time_in_top;
    };
   
    struct pretty_domain_info
    {
        string                                      domain_name;
        address                                     owner;
        fc::optional<public_key_type>               signin_key;
        fc::time_point_sec                          last_renewed;
        domain_record::domain_state_type            domain_state;
        optional<pretty_domain_auction_summary>     auction_info;
    };

    struct pretty_domain_offer
    {
        string                                domain_name;
        double                                price;
        address                               offer_address;
    };

    struct pretty_account_edge
    {
        string     from;
        string     to;
        string     edge_name;
        variant    value;
    };

}} //bts::wallet

FC_REFLECT( bts::wallet::pretty_domain_info, (domain_name)(owner)(signin_key)(last_renewed)(domain_state)(auction_info) );
FC_REFLECT( bts::wallet::pretty_domain_offer, (domain_name)(price)(offer_address) );
FC_REFLECT( bts::wallet::pretty_domain_auction_summary, (domain_name)(last_bid_price)(next_required_bid_price)(last_bid_time)(time_in_top) );
FC_REFLECT( bts::wallet::pretty_account_edge, (from)(to)(edge_name)(value) );
