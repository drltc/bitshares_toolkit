#pragma once

#define P2P_MIN_INITIAL_BID (10)
#define P2P_MIN_BID_INCREASE(amount) ((amount * 101) / 100)
#define P2P_REQUIRED_BID_DIFF_RATIO (1)  // my_required = prev_bid + R*(prev_bid - prev_required)

#define P2P_RETURN_WITH_PENALTY(amount) ((amount * 95) / 100)
#define P2P_RETURN_WITH_KICKBACK(last_bid, current_bid) (last_bid + ((current_bid - last_bid) / 10))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define P2P_NEXT_REQ_BID(req, bid) MAX((bid + (P2P_REQUIRED_BID_DIFF_RATIO*(bid - req))), P2P_MIN_BID_INCREASE(bid))

#define P2P_AUCTION_DURATION_SECS (60 * 1) //(60*60*24  * 3)
#define P2P_EXPIRE_DURATION_SECS (60 * 60 * 24) //(60*60*24  * 365)

#define P2P_MIN_DOMAIN_NAME_SIZE (5)
#define P2P_MAX_DOMAIN_NAME_SIZE (63)

#define P2P_DILUTION_RATE (5000000) // extra block reward = (max_supply - current_supply) / this


#define KEYID_HARDFORK_1 (15000)
#define KEYID_HARDFORK_2 (107500)


#define KEYID_EXTRA_FEE_0 (100)
#define KEYID_EXTRA_FEE_1 (10000000)
#define KEYID_EXTRA_FEE_2 (50000000)


#define KEYID_MAX_EDGE_NAME_SIZE (64)
#define KEYID_MAX_EDGE_VALUE_SIZE (512)

#define DNS_EDGE_VALIDATION_PREFIX ("_chain")

//  Feed IDs for dac parameters
#define DNS_PARAM_SPOTLIGHT (100001)


#define DNS_LEASE_AUCTION_DURATION_SECS (60)  // 1 month  ?
#define DNS_MIN_BID_INCREASE(amount) ((amount * 105)/100)


