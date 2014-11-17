
"""
1. Create genesis block files
2. Produce blocks
"""

import io
import json
import os
import subprocess

def mkdir_p(path):
    try:
        os.makedirs(path)
    except FileExistsError:
        pass
    return

class TestFixture(object):
    def __init__(self):
        self.genesis_filename = "genesis.json"
        self.basedir = "tmp"
        return

    def create_genesis_file(self):
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

        balances = [[key[i]["pts_address"], 100000000000] for i in range(101)]
        
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

        mkdir_p(os.path.dirname(self.get_genesis_path()))
        with open(self.get_genesis_path(), "w") as f:
            json.dump(genesis_json, f, indent=4)
        return

    def get_genesis_path(self):
        return os.path.join(self.basedir, self.genesis_filename)

    def launch(self, client_count):
        for clientnum in range(client_count):
            Node(clientnum).launch()
        return

class Node(object):
    def __init__(self,
        clientnum,
        basedir="tmp",
        genesis_filename="genesis.json",
        ):

        self.clientnum = clientnum
        self.basedir = basedir
        self.genesis_filename = genesis_filename
        self.httpport = 9100+clientnum
        self.p2pport = 9200+clientnum
        self.rpcport = 9300+clientnum
        #self.csport = 9400+clientnum
        self.csport = None
        return

    def get_data_dir(self):
        return os.path.join(self.basedir, str(self.clientnum))

    def get_genesis_path(self):
        return os.path.join(self.basedir, self.genesis_filename)

    def launch(self):
        args = [
         "programs/client/bitshares_client",
         "--data-dir", self.get_data_dir(),
         "--genesis-config", self.get_genesis_path(),
         "--min-delegate-connection-count", "0",
         "--server",
         "--rpc-user", "user",
         "--rpc-password", "pass",
        ]
        if self.httpport is not None:
            args.extend(["--httpport", str(self.httpport)])
        if self.p2pport is not None:
            args.extend(["--p2p-port", str(self.p2pport)])
        if self.rpcport is not None:
            args.extend(["--rpcport", str(self.rpcport)])
        if self.csport is not None:
            args.extend(["--chain-server-port", str(self.csport)])
        if self.clientnum > 0:
            args.extend(["--connect-to", "127.0.0.1:9200"])
        print(os.getcwd())
        print(args)
        return

tf = TestFixture()
tf.create_genesis_file()
tf.launch(2)
