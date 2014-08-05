#include <bts/blockchain/dns_record.hpp>
#include <bts/blockchain/types.hpp>

namespace bts { namespace blockchain {

    domain_record::domain_state_type domain_record::get_true_state(uint32_t sec_since_epoch) const
    {
        if (domain_state_type(this->state) == owned)
        {
            if (sec_since_epoch > last_update + P2P_EXPIRE_DURATION_SECS)
                return unclaimed;
            return owned;
        }
        return this->state;
    }

}}
