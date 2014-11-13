
"""
1. Create genesis block files
2. Produce blocks
"""

import io
import json
import subprocess

def create_genesis_file():
    # programs/utils/bts_create_key --count=101 --seed=test-delegate-
    keydata = subprocess.check_output(
        [
         "programs/utils/bts_create_key",
         "--count=200",
         "--seed=testkey-",
        ],
        )
    print("here is the key data:")
    key = json.loads(keydata.decode())

    balances = [[key[i]["pts_address"], 100000000000] for i in x]
    
    genesis_json = {
      "timestamp" : "2014-11-13T15:00:00",
      
      "market_assets" : [],
      
      "names" :
      [
        {
          "name" : "init"+str(i),
          "owner" : key[i]["public_key"],
          "delegate_pay_rate" : 1,
        }
        for i in range(101)
      ],

      "balances" : balances,
    }
    
    bts_sharedrop = []

    return genesis_json

import sys

with open("genesis.json", "w") as f:
    json.dump(f, create_genesis_file(), indent=4)
