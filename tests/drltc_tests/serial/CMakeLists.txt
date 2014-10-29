
add_executable( serial_tests serial.cpp )
target_link_libraries( serial_tests deterministic_openssl_rand bts_client bts_cli bts_wallet bts_blockchain bts_net bitcoin fc )
