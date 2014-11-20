
"""
1. Create genesis block files
2. Produce blocks
"""

import io
import json
import os
import re
import subprocess
import sys
import readline
import traceback

import tornado.process
import tornado.gen
import tornado.httpclient

from tornado.ioloop import IOLoop
from tornado.gen import coroutine, Task

@coroutine
def call_cmd(cmd, stdin_data=None):
    """
    Wrapper around subprocess call using Tornado's Subprocess class.
    """
    try:
        p = tornado.process.Subprocess(
            cmd,
            stdin=subprocess.PIPE,
            stdout=tornado.process.Subprocess.STREAM,
            stderr=tornado.process.Subprocess.STREAM,
        )
    except OSError as e:
        raise Return((None, e))

    if stdin_data:
        p.stdin.write(stdin_data)
        p.stdin.flush()
        p.stdin.close()

    result, error = yield [
        Task(p.stdout.read_until_close),
        Task(p.stderr.read_until_close),
    ]

    raise tornado.gen.Return((result, error))

def mkdir_p(path):
    try:
        os.makedirs(path)
    except FileExistsError:
        pass
    return

DELEGATE_COUNT = 101

re_number = re.compile(r"^\s*([0-9]+)")

class TestFixture(object):
    def __init__(self):
        self.genesis_filename = "genesis.json"
        self.basedir = "tmp"
        self.node = []
        self.delegate2nodeid = DELEGATE_COUNT*[0]
        self.name2privkey = {}
        self.genesis_timestamp = "2014-11-13T15:00:00"
        self.nextblock = 1
        return

    @coroutine
    def create_genesis_file(self):
        # programs/utils/bts_create_key --count=101 --seed=test-delegate-
        keyout, keyerr = yield call_cmd(
            [
             "programs/utils/bts_create_key",
             "--count=200",
             "--seed=testkey-",
            ],
            )
        key = json.loads(keyout.decode())

        balances =  [[key[0]["pts_address"], 10000 * 2 * (10**8)]]
        balances += [[key[i]["pts_address"], 100000000000] for i in range(1, DELEGATE_COUNT)]
        
        genesis_json = {
          "timestamp" : self.genesis_timestamp,
          
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
            
        for i in range(DELEGATE_COUNT):
            self.name2privkey["init"+str(i)] = key[i]["wif_private_key"]
        return

    def get_genesis_path(self):
        return os.path.join(self.basedir, self.genesis_filename)

    @coroutine
    def launch(self, client_count):
        newnodes = [Node(i, cmd_loop=self.cmd_loop) for i in range(client_count)]
        self.node.extend(newnodes)
        n0 = newnodes[0]
        yield n0.launch()
        yield n0.p2p_server_up
        tasks = [
                 node.launch() for node in newnodes[1:]
                ]
        yield tasks
        return

    @coroutine
    def clients(self, cmd):
        u = cmd.split()
        for n in self.node:
            yield n.run_cmd(*u)
        return

    @coroutine
    def create_wallets(self):
        yield self.clients("debug_start_simulated_time "+self.genesis_timestamp)
        yield self.clients("debug_advance_time 1 seconds")
        for i in range(1, len(self.node)):
            yield self.node[i].run_cmd(
                "wallet_create", "default", "walletpassword"
            )
            yield self.node[i].run_cmd(
                "wallet_unlock", "9999999", "walletpassword"
            )
        return

    @coroutine
    def register_delegates(self):

        if os.path.exists("delegate-wallet-bup.json"):
            yield self.node[0].run_cmd("wallet_backup_restore",
                "delegate-wallet-bup.json",
                "default",
                "walletpassword",
                )
            return

        yield self.node[0].run_cmd(
            "wallet_create", "default", "walletpassword"
        )
        yield self.node[0].run_cmd(
            "wallet_unlock", "9999999", "walletpassword"
        )
        for i in range(DELEGATE_COUNT):
            n = self.node[self.delegate2nodeid[i]]
            yield n.run_cmd("wallet_import_private_key",
                self.name2privkey["init"+str(i)],
                "init"+str(i),
                True,
                True,
                )
            yield n.run_cmd("wallet_delegate_set_block_production",
                "init"+str(i),
                True,
                )
        
        yield self.node[0].run_cmd("wallet_backup_create",
            "delegate-wallet-bup.json",
            )
        return

    @coroutine
    def delegates(self, cmd):
        u = cmd.split()
        for i in range(DELEGATE_COUNT):
            n = self.node[self.delegate2nodeid[i]]
            yield n.run_cmd(*u)
        return

    @coroutine
    def step(self, steps=1):
        for i in range(steps):
            yield self.clients("debug_advance_time 1 blocks")
            yield self.clients("debug_wait_for_block_by_number "+str(self.nextblock))
            self.nextblock += 1
        return

    @coroutine
    def setup_angel(self):
        n = self.node[self.delegate2nodeid[0]]
        n_alice = self.node[1]
        n_bob   = self.node[2]
        alice_key = yield n_alice.run_cmd(
            "wallet_account_create", "alice",
            )
        bob_key   = yield n_bob.run_cmd(
            "wallet_account_create", "bob",
            )
        yield n.run_cmd(
            "wallet_add_contact_account", "alice", alice_key
            )
        yield n.run_cmd(
            "wallet_add_contact_account", "bob", bob_key
            )
        yield n.run_cmd(
            "wallet_account_register", "alice", "init0"
            )
        yield n.run_cmd(
            "wallet_account_register", "bob", "init0"
            )
        yield self.step()
        yield self.cmd_loop()
        return

    @coroutine
    def simple_transfer(self):
        xfer = yield self.angel(
            "wallet_transfer 100 XTS $acct alice hello_world vote_none",
            )
        yield self.step()
        alice_balance = yield self.alice(
            "wallet_account_balance"
            )
        self.assert_equal(alice_balance, ["alice",[0,100 * 10000]])
        return

    @coroutine
    def run_cmd_as(self, ename, cmd):
        for o in self.get_nodes(ename):
            cmd_sub = cmd.replace("$acct", o["acct"])
            n = self.node[o["node_id"]]
            yield n.run_cmd(*cmd.split(" "))
        return

    @coroutine
    def angel(self, cmd):
        result = yield self.run_cmd_as("angel", cmd)
        return result

    @coroutine
    def alice(self, cmd):
        result = yield self.run_cmd_as("alice", cmd)
        return result
        
    @coroutine
    def bob(self, cmd):
        result = yield self.run_cmd_as("bob", cmd)
        return result

    def assert_equal(self, a, b):
        if a == b:
            return
        print(a)
        print(b)
        raise RuntimeError("Failed")

    def get_nodes(self, node_exp):
        m = re_number.match(node_exp)
        if m is not None:
            u = node_exp.split(",")
            for i in range(len(u)):
                yield dict(node_id=int(u[i]), acct="")
        elif node_exp == "alice":
            yield dict(node_id=1, acct="alice")
        elif node_exp == "bob":
            yield dict(node_id=2, acct="bob")
        elif node_exp == "delegates":
            for i in range(len(DELEGATE_COUNT)):
                yield dict(
                    node_id=self.delegate2nodeid[i],
                    acct="init"+str(i))
        elif node_exp == "angel":
            yield dict(node_id=0, acct="init0")
        elif node_exp == "none":
            pass
        else:
            pass
        return

    @coroutine
    def cmd_loop(self):
        active_nodes = None
        while True:
            prompt = ">>> "
            if active_nodes is not None:
                prompt = "("+active_nodes+") >>> "
            cmd = input(prompt)
            if cmd[0] == ">":
                active_nodes = cmd[1:]
            elif cmd == "quit":
                break
            else:
                for o in self.get_nodes(active_nodes):
                    cmd_sub = cmd.replace("$acct", o["acct"])
                    n = self.node[o["node_id"]]
                    yield n.run_cmd(*cmd.split(" "))
        return

re_http_start = re.compile("^Starting HTTP JSON RPC server on port ([0-9]+).*$")
re_p2p_start = re.compile("^Listening for P2P connections on port ([0-9]+).*$")

class Node(object):
    def __init__(self,
        clientnum,
        basedir="tmp",
        genesis_filename="genesis.json",
        io_loop=None,
        cmd_loop=None,
        ):

        if io_loop is not None:
            self.io_loop = io_loop
        else:
            self.io_loop = tornado.ioloop.IOLoop.instance()

        self.clientnum = clientnum
        self.basedir = basedir
        self.genesis_filename = genesis_filename
        self.httpport = 9100+clientnum
        self.p2pport = 9200+clientnum
        self.rpcport = 9300+clientnum
        #self.csport = 9400+clientnum
        self.csport = None

        self.process = None
        
        self.http_server_up = tornado.concurrent.Future()
        self.http_client_up = tornado.concurrent.Future()
        self.p2p_server_up = tornado.concurrent.Future()
        
        self.http_client = tornado.httpclient.AsyncHTTPClient(
            force_instance=True,
            defaults=dict(
                user_agent="drltc-BTS-API-Tester",
            )
            )
            
        self.next_request_id = 1
        self.cmd_loop = cmd_loop
        return

    def get_data_dir(self):
        return os.path.join(self.basedir, str(self.clientnum))

    def get_genesis_path(self):
        return os.path.join(self.basedir, self.genesis_filename)

    @coroutine
    def launch(self):
        print("in Node.launch()")
        args = [
         "programs/client/bitshares_client",
         "--data-dir", self.get_data_dir(),
         "--genesis-config", self.get_genesis_path(),
         "--min-delegate-connection-count", "0",
         "--server",
         "--rpcuser", "user",
         "--rpcpassword", "pass",
         "--upnp", "false",
         "--disable-default-peers",
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
        
        self.process = tornado.process.Subprocess(args,
            io_loop=self.io_loop,
            stdin=tornado.process.Subprocess.STREAM,
            stdout=tornado.process.Subprocess.STREAM,
            stderr=tornado.process.Subprocess.STREAM,
            )
        self.io_loop.add_callback(self.read_stdout_forever)
        self.io_loop.add_callback(self.read_stderr_forever)
        return

    @coroutine
    def read_stdout_forever(self):
        seen_http_start = False
        seen_p2p_start = False
        while True:
            try:
                line = yield self.process.stdout.read_until(b"\n")
                print(str(self.clientnum)+": "+line.decode(), end="")
            except tornado.iostream.StreamClosedError:
                print(str(self.clientnum)+": finished with StreamClosedError")
                break
            if not seen_http_start:
                sline = line.decode()
                m = re_http_start.match(sline)
                if m is not None:
                    port = int(m.group(1))
                    # enable future
                    self.http_server_up.set_result(port)
                    seen_http_start = True
            if not seen_p2p_start:
                sline = line.decode()
                m = re_p2p_start.match(sline)
                if m is not None:
                    port = int(m.group(1))
                    # enable future
                    self.p2p_server_up.set_result(port)
                    seen_p2p_start = True
        return

    @coroutine
    def read_stderr_forever(self):
        while True:
            line = yield self.process.stderr.read_until(b"\n")
            print("client: "+line.decode())
        return

    @coroutine
    def run_cmd(self, method, *params):
        req_id = self.alloc_request_id()
        req_body = json.dumps(
        {
            "method" : method,
            "params" : params,
            "id" : req_id,
        })
        yield self.http_server_up
        print(str(self.clientnum)+"> "+req_body)
        try:
            response = yield self.http_client.fetch(
                "http://127.0.0.1:"+str(self.httpport)+"/rpc",
                method="POST",
                auth_username="user",
                auth_password="pass",
                auth_mode="basic",
                user_agent="drltc-bts-api-tester",
                body=req_body,
                headers={
                "Content-Type" : "application/json",
                },
                )
        except tornado.httpclient.HTTPError as e:
            print(str(self.clientnum)+"! "+str(e.code))
            print(e.response.body)
            yield self.cmd_loop()
            sys.exit(1)
        print(str(self.clientnum)+"< ", response.body)
        return json.loads(response.body.decode("utf-8"))["result"]

    def alloc_request_id(self):
        result = self.next_request_id
        self.next_request_id = result+1
        return result

@coroutine
def _main():
    tf = TestFixture()
    print("TestFixture created")
    yield tf.create_genesis_file()
    print("Genesis file created")
    yield tf.launch(3)
    yield tf.create_wallets()
    yield tf.register_delegates()
    yield tf.setup_angel()

    return

@coroutine
def main():
    try:
        yield _main()
    except Exception as e:
        print("caught exception")
        print(traceback.format_exc())
    finally:
        the_io_loop.stop()
    return

if __name__ == "__main__":
    the_io_loop = tornado.ioloop.IOLoop.instance()
    the_io_loop.add_callback(main)
    the_io_loop.start()
