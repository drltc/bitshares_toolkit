
"""
1. Create genesis block files
2. Produce blocks
"""

import io
import json
import os
import re
import subprocess
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

class TestFixture(object):
    def __init__(self):
        self.genesis_filename = "genesis.json"
        self.basedir = "tmp"
        self.node = []
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

    @coroutine
    def launch(self, client_count):
        newnodes = [Node(i) for i in range(client_count)]
        self.node.extend(newnodes)
        tasks = [
                 node.launch() for node in newnodes
                ]
        yield tasks
        return

re_http_start = re.compile("^Starting HTTP JSON RPC server on port ([0-9]+).*$")

class Node(object):
    def __init__(self,
        clientnum,
        basedir="tmp",
        genesis_filename="genesis.json",
        io_loop=None,
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
        
        self.http_client = tornado.httpclient.AsyncHTTPClient(
            force_instance=True,
            defaults=dict(
                user_agent="drltc-BTS-API-Tester",
            )
            )
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
        self.io_loop.add_callback(self.start_http_client)
        return

    @coroutine
    def start_http_client(self):
        yield self.http_server_up
        self.socket = socket.socket(
            socket.AF_INET,
            socket.SOCK_STREAM,
            0,
            )
        self.http_conn = tornado.iostream.IOStream(self.socket)
        yield self.http_conn.connect(("127.0.0.1", self.httpport))
        return

    @coroutine
    def read_stdout_forever(self):
        seen_http_start = False
        while True:
            try:
                line = yield self.process.stdout.read_until(b"\n")
                print("client: "+line.decode())
            except tornado.iostream.StreamClosedError:
                print("finished with StreamClosedError")
                break
            if not seen_http_start:
                sline = line.decode()
                m = re_http_start.match(sline)
                if m is not None:
                    port = int(m.group(1))
                    print("saw HTTP port", port)
                    # enable future
                    self.http_server_up.set_result(port)
                    seen_http_start = True
        return

    @coroutine
    def read_stderr_forever(self):
        while True:
            line = yield self.process.stderr.read_until(b"\n")
            print("client: "+line.decode())
        return

    @coroutine
    def run_cmd(self, cmd):
        yield self.http_server_up
        response = yield self.http_client.fetch(
            "http://127.0.0.1:"+str(self.httpport)+"/rpc",
            method="POST",
            auth_username="user",
            auth_password="pass",
            auth_mode="basic",
            user_agent="drltc-bts-api-tester",
            headers={
            "content-type" : "application/json",
            },
            )
        return

@coroutine
def _main():
    tf = TestFixture()
    print("TestFixture created")
    yield tf.create_genesis_file()
    print("Genesis file created")
    yield tf.launch(2)
    info0 = yield tf.node[0].run_cmd("info")
    print("info0:")
    print(info0)
    info1 = yield tf.node[1].run_cmd("info")
    print("info1:")
    print(info1)
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
