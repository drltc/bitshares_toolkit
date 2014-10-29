
#include <iostream>

#include <fc/crypto/elliptic.hpp>
#include <fc/io/sstream.hpp>
#include <bts/blockchain/transaction.hpp>
#include <bts/wallet/wallet.hpp>

using namespace std;

class deterministic_key_generator
{
public:
    deterministic_key_generator(uint64_t seed)
    {
        fc::sha256::encoder enc;
        fc::raw::pack(enc, seed);
        this->state = enc.result();
        return;
    }
    
    virtual ~deterministic_key_generator();

    fc::ecc::private_key next();

private:
    fc::sha256 state;
};

deterministic_key_generator::~deterministic_key_generator()
{
    return;
}

fc::ecc::private_key deterministic_key_generator::next()
{
    fc::ecc::private_key result =
        fc::ecc::private_key::regenerate(this->state);
    this->state = fc::sha256::hash(this->state.data(), 32);
    return result;
}

fc::ecc::private_key deterministic_key_gen(uint64_t index)
{
    fc::sha256::encoder enc;
    fc::raw::pack(enc, index);
    return fc::ecc::private_key::regenerate(enc.result());
}

void print_hex_char(char c)
{
    static const char hex_digit[] = "0123456789abcdef";
    cout << hex_digit[(c >> 4) & 0x0F];
    cout << hex_digit[(c     ) & 0x0F];
    return;
}

void print_hex_string(fc::string &s)
{
    int i = 0;
    cout << "s.length() == " << s.length() << '\n';
    for(char &c : s)        // C++11 range loop
    {
        cout << "c == " << ((int) c) << '\n';
        if (i > 0)
            cout << ' ';
        print_hex_char(c);
        i++;
        if (i == 0x10)
        {
            cout << '\n';
            i = 0;
        }
    }
    return;
}

template<typename T> void show_serialization(T data)
{
    fc::stringstream ss;
    fc::string str_data;
    //ss << data;                    //THIS DOESN'T WORK
    fc::raw::pack(ss, data);        //THIS DOESN'T WORK EITHER
    ss.str(str_data);
    print_hex_string(str_data);
    return;
}

int main(int argc, char **argv, char **envp)
{
    try{
    // serial representation of data
    bts::blockchain::transaction tx;

    cout << "creating keygen\n";

    auto keygen = deterministic_key_generator(1);

    cout << "creating pk's\n";
    
    fc::ecc::private_key pk_0 = keygen.next();
    fc::ecc::private_key pk_1 = keygen.next();
    fc::ecc::private_key pk_2 = keygen.next();
    fc::ecc::private_key pk_3 = keygen.next();

    cout << "calling show_serialization\n";
    
    show_serialization(pk_0);

    // serialization of withdraw_condition
    //address::address( const withdraw_condition& condition )
    //fc::raw::pack( enc, condition );
    //
    //withdraw_condition condition = withdraw_condition(
    //    withdraw_with_signature(owner),
    //    balance_arg.asset_id,
    //    delegate_id
    //    );

    // owner is address
    // make address from base58 string
    // BTS_ADDRESS_PREFIX
    } FC_LOG_AND_RETHROW();

    cout << "done\n";

    return 0;
}
