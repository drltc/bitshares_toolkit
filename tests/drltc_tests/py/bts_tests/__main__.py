
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
    key_data = json.loads(keydata.decode())
    print(key_data)
    return

import sys

create_genesis_file()
