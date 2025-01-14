#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2017-2022 The Particl Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test addressindex generation and fetching
#

from test_framework.test_particl import ParticlTestFramework, bytes_to_wif
from test_framework.util import assert_equal
from test_framework.script import taproot_construct
from test_framework.key import generate_privkey, compute_xonly_pubkey
from test_framework.segwit_addr import encode_segwit_address


class AddressIndexTest(ParticlTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.extra_args = [
            # Nodes 0/1 are "wallet" nodes
            ['-debug',],
            ['-debug','-addressindex'],
            # Nodes 2/3 are used for testing
            ['-debug','-addressindex',],
            ['-debug','-addressindex'],]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)
        self.connect_nodes_bi(0, 3)
        self.connect_nodes_bi(1, 3)

        self.sync_all()

    def run_test(self):
        nodes = self.nodes

        # Stop staking
        for i in range(len(nodes)):
            nodes[i].reservebalance(True, 10000000)

        self.import_genesis_coins_a(nodes[0])

        nodes[1].extkeyimportmaster('graine article givre hublot encadrer admirer stipuler capsule acajou paisible soutirer organe')
        nodes[2].extkeyimportmaster('sección grito médula hecho pauta posada nueve ebrio bruto buceo baúl mitad')
        nodes[3].extkeyimportmaster('けっこん　ゆそう　へいねつ　しあわせ　ちまた　きつね　たんたい　むかし　たかい　のいず　こわもて　けんこう')

        addrs = [nodes[1].getnewaddress() for i in range(3)]

        ms1 = nodes[1].addmultisigaddress_part(2, addrs)['address']
        assert (ms1 == 'r8L81gLiWg46j5EGfZSp2JHmA9hBgLbHuf')  # rFHaEuXkYpNUYpMMY3kMkDdayQxpc7ozti

        addr1 = nodes[2].getnewaddress()
        assert (addr1 == 'pqZDE7YNWv5PJWidiaEG8tqfebkd6PNZDV')  # pcX1WHotKuQwFypDf1ZkJrh81J1DS7DfXd
        addr2 = nodes[3].getnewaddress()
        assert (addr2 == 'pqavEUgLCZeGh8o9sTcCfYVAsrTgnQTUsK')

        self.sync_all()
        chain_height = nodes[1].getblockcount()
        assert_equal(chain_height, 0)

        assert_equal(nodes[1].getbalance(), 0)
        assert_equal(nodes[2].getbalance(), 0)

        balance0 = nodes[1].getaddressbalance("r8L81gLiWg46j5EGfZSp2JHmA9hBgLbHuf")
        assert_equal(balance0["balance"], 0)

        # Check p2pkh and p2sh address indexes
        self.log.info("Testing p2pkh and p2sh address index...")

        txid0 = nodes[0].sendtoaddress("pqZDE7YNWv5PJWidiaEG8tqfebkd6PNZDV", 10)
        self.stakeToHeight(1, fSync=False)
        txidb0 = nodes[0].sendtoaddress("r8L81gLiWg46j5EGfZSp2JHmA9hBgLbHuf", 10)
        self.stakeToHeight(2, fSync=False)
        txid1 = nodes[0].sendtoaddress("pqZDE7YNWv5PJWidiaEG8tqfebkd6PNZDV", 15)
        self.stakeToHeight(3, fSync=False)
        txidb1 = nodes[0].sendtoaddress("r8L81gLiWg46j5EGfZSp2JHmA9hBgLbHuf", 15)
        self.stakeToHeight(4, fSync=False)
        txid2 = nodes[0].sendtoaddress("pqZDE7YNWv5PJWidiaEG8tqfebkd6PNZDV", 20)
        self.stakeToHeight(5, fSync=False)
        txidb2 = nodes[0].sendtoaddress("r8L81gLiWg46j5EGfZSp2JHmA9hBgLbHuf", 20)
        self.stakeToHeight(6)

        txids = nodes[1].getaddresstxids("pqZDE7YNWv5PJWidiaEG8tqfebkd6PNZDV")
        assert_equal(len(txids), 3)
        assert_equal(txids[0], txid0)
        assert_equal(txids[1], txid1)
        assert_equal(txids[2], txid2)

        txidsb = nodes[1].getaddresstxids("r8L81gLiWg46j5EGfZSp2JHmA9hBgLbHuf")
        assert_equal(len(txidsb), 3)
        assert_equal(txidsb[0], txidb0)
        assert_equal(txidsb[1], txidb1)
        assert_equal(txidsb[2], txidb2)


        # Check that limiting by height works
        self.log.info("Testing querying txids by range of block heights..")
        # Note start and end parameters must be > 0 to apply
        height_txids = nodes[1].getaddresstxids({
            "addresses": ["r8L81gLiWg46j5EGfZSp2JHmA9hBgLbHuf"],
            "start": 3,
            "end": 4
        })
        assert_equal(len(height_txids), 1)
        #assert_equal(height_txids[0], txidb0)
        assert_equal(height_txids[0], txidb1)

        # Check that multiple addresses works
        multitxids = nodes[1].getaddresstxids({"addresses": ["r8L81gLiWg46j5EGfZSp2JHmA9hBgLbHuf", "pqZDE7YNWv5PJWidiaEG8tqfebkd6PNZDV"]})
        assert_equal(len(multitxids), 6)
        assert_equal(multitxids[0], txid0)
        assert_equal(multitxids[1], txidb0)
        assert_equal(multitxids[2], txid1)
        assert_equal(multitxids[3], txidb1)
        assert_equal(multitxids[4], txid2)
        assert_equal(multitxids[5], txidb2)

        # Check that balances are correct
        balance0 = nodes[1].getaddressbalance("r8L81gLiWg46j5EGfZSp2JHmA9hBgLbHuf")
        assert_equal(balance0["balance"], 45 * 100000000)


        # Check that outputs with the same address will only return one txid
        self.log.info("Testing for txid uniqueness...")

        inputs = []
        outputs = {'pqZDE7YNWv5PJWidiaEG8tqfebkd6PNZDV':1,'pqavEUgLCZeGh8o9sTcCfYVAsrTgnQTUsK':1}
        tx = nodes[0].createrawtransaction(inputs, outputs)

        # modified outputs to go to the same address
        tx = 'a0000000000000020100e1f505000000001976a914e317164ad324e5ec2f8b5de080f0cb614042982d88ac0100e1f505000000001976a914e317164ad324e5ec2f8b5de080f0cb614042982d88ac'

        txfunded = nodes[0].fundrawtransaction(tx)
        txsigned = nodes[0].signrawtransactionwithwallet(txfunded['hex'])
        sent_txid = nodes[0].sendrawtransaction(txsigned['hex'], 0)

        self.stakeBlocks(1)

        txidsmany = nodes[1].getaddresstxids("pqavEUgLCZeGh8o9sTcCfYVAsrTgnQTUsK")
        assert_equal(len(txidsmany), 1)
        assert_equal(txidsmany[0], sent_txid)


        # Check that balances are correct
        self.log.info("Testing balances...")
        balance0 = nodes[1].getaddressbalance("pqavEUgLCZeGh8o9sTcCfYVAsrTgnQTUsK")
        assert_equal(balance0["balance"], 2 * 100000000)


        balance0 = nodes[1].getaddressbalance("pqZDE7YNWv5PJWidiaEG8tqfebkd6PNZDV")
        assert_equal(balance0["balance"], 45 * 100000000)


        inputs = []
        outputs = {'pqavEUgLCZeGh8o9sTcCfYVAsrTgnQTUsK':1}
        tx = nodes[2].createrawtransaction(inputs, outputs)
        txfunded = nodes[2].fundrawtransaction(tx)

        txsigned = nodes[2].signrawtransactionwithwallet(txfunded['hex'])
        sent_txid = nodes[2].sendrawtransaction(txsigned['hex'], 0)

        self.sync_all()
        self.stakeBlocks(1)

        txidsmany = nodes[1].getaddresstxids("pqavEUgLCZeGh8o9sTcCfYVAsrTgnQTUsK")
        assert_equal(len(txidsmany), 2)
        assert_equal(txidsmany[1], sent_txid)


        balance0 = nodes[1].getaddressbalance('pqZDE7YNWv5PJWidiaEG8tqfebkd6PNZDV')
        assert (balance0["balance"] < 45 * 100000000)


        # Check that deltas are returned correctly
        deltas = nodes[1].getaddressdeltas({"addresses": ['pqavEUgLCZeGh8o9sTcCfYVAsrTgnQTUsK'], "start": 1, "end": 200})
        balance3 = 0
        for delta in deltas:
            balance3 += delta["satoshis"]
        assert_equal(balance3, 300000000)
        assert_equal(deltas[0]["address"], 'pqavEUgLCZeGh8o9sTcCfYVAsrTgnQTUsK')
        #assert_equal(deltas[0]["blockindex"], 1)


        address2 = 'pqZDE7YNWv5PJWidiaEG8tqfebkd6PNZDV'
        # Check that entire range will be queried
        deltasAll = nodes[1].getaddressdeltas({"addresses": [address2]})
        assert_equal(len(deltasAll), 4)

        # Check that deltas can be returned from range of block heights
        deltas = nodes[1].getaddressdeltas({"addresses": [address2], "start": 3, "end": 3})
        assert_equal(len(deltas), 1)

        # Check that unspent outputs can be queried
        self.log.info("Testing utxos...")
        utxos = nodes[1].getaddressutxos({"addresses": [address2]})
        assert_equal(len(utxos), 2)
        assert_equal(utxos[0]["satoshis"], 1500000000)

        # Check that indexes will be updated with a reorg
        self.log.info("Testing reorg...")
        height_before = nodes[1].getblockcount()
        best_hash = nodes[0].getbestblockhash()
        nodes[0].invalidateblock(best_hash)
        nodes[1].invalidateblock(best_hash)
        nodes[2].invalidateblock(best_hash)
        nodes[3].invalidateblock(best_hash)
        self.sync_all()
        assert (nodes[1].getblockcount() == height_before - 1)

        balance4 = nodes[1].getaddressbalance(address2)
        assert_equal(balance4['balance'], 4500000000)

        utxos2 = nodes[1].getaddressutxos({"addresses": [address2]})
        assert_equal(len(utxos2), 3)
        assert_equal(utxos2[0]["satoshis"], 1000000000)

        # Check sorting of utxos
        self.log.info("Testing sorting of utxos...")

        self.stakeBlocks(1)

        txidsort1 = nodes[0].sendtoaddress(address2, 50)
        self.stakeBlocks(1)
        txidsort2 = nodes[0].sendtoaddress(address2, 50)
        self.stakeBlocks(1)

        utxos3 = nodes[1].getaddressutxos({"addresses": [address2]})
        assert_equal(len(utxos3), 4)
        assert_equal(utxos3[0]["height"], 3)
        assert_equal(utxos3[1]["height"], 5)
        assert_equal(utxos3[2]["height"], 9)
        assert_equal(utxos3[3]["height"], 10)

        assert (utxos3[2]['txid'] == txidsort1)
        assert (utxos3[3]['txid'] == txidsort2)

        # Check mempool indexing
        self.log.info("Testing mempool indexing...")

        address3 = nodes[3].getnewaddress()

        txidsort1 = nodes[2].sendtoaddress(address3, 1)
        self.wait_for_mempool(nodes[1], txidsort1)
        txidsort2 = nodes[2].sendtoaddress(address3, 1)
        self.wait_for_mempool(nodes[1], txidsort2)
        txidsort3 = nodes[2].sendtoaddress(address3, 1)

        mempool = nodes[2].getaddressmempool({"addresses": [address3]})
        assert_equal(len(mempool), 3)
        assert (mempool[0]['txid'] == txidsort1)
        assert_equal(mempool[0]['address'], address3)
        assert (mempool[1]['txid'] == txidsort2)
        assert_equal(mempool[1]['address'], address3)
        assert (mempool[2]['txid'] == txidsort3)
        assert_equal(mempool[2]['address'], address3)

        self.sync_all()
        self.stakeBlocks(1)
        mempool = nodes[2].getaddressmempool({"addresses": [address3]})
        assert_equal(len(mempool), 0)

        # sending and receiving to the same address, in the same txn
        self.log.info("Testing sending and receiving to the same address...")

        address4 = nodes[2].getnewaddress()
        txid_in = nodes[1].sendtoaddress(address4, 1)
        self.sync_all()
        self.stakeBlocks(1)
        mempool = nodes[2].getaddressmempool({"addresses": [address4]})
        assert_equal(len(mempool), 0)
        utxos4 = nodes[2].getaddressutxos({"addresses": [address4]})
        assert (len(utxos4) == 1)
        assert (txid_in == utxos4[0]['txid'])
        inputs = [{'txid': utxos4[0]['txid'], 'vout': utxos4[0]['outputIndex']}]
        outputs = {address4: 0.99}
        tx = nodes[2].createrawtransaction(inputs, outputs)
        tx = nodes[2].signrawtransactionwithwallet(tx)['hex']
        nodes[2].sendrawtransaction(tx)

        mempool_deltas = nodes[2].getaddressmempool({"addresses": [address4]})
        assert_equal(len(mempool_deltas), 2)

        # Include chaininfo in results
        self.log.info("Testing results with chain info...")

        deltas_with_info = nodes[1].getaddressdeltas({
            "addresses": [address2],
            "start": 1,
            "end": 10,
            "chainInfo": True
        })
        start_block_hash = nodes[1].getblockhash(1)
        end_block_hash = nodes[1].getblockhash(10)
        assert_equal(deltas_with_info["start"]["height"], 1)
        assert_equal(deltas_with_info["start"]["hash"], start_block_hash)
        assert_equal(deltas_with_info["end"]["height"], 10)
        assert_equal(deltas_with_info["end"]["hash"], end_block_hash)

        utxos_with_info = nodes[1].getaddressutxos({"addresses": [address2], "chainInfo": True})
        assert (len(utxos_with_info['utxos']) == 2)
        assert (utxos_with_info['utxos'][0]['height'] == 9)

        # 256bit addresses
        self.log.info("Testing 256bit addresses...")

        addr256 = nodes[3].getnewaddress("", "false", "false", "true")

        nodes[3].sendtoaddress(addr256, 2.56)
        mempool = nodes[3].getaddressmempool({"addresses": [addr256]})
        assert_equal(len(mempool), 1)

        self.sync_all()
        self.stakeBlocks(1)

        ro = nodes[3].getaddresstxids(addr256)
        assert_equal(len(ro), 1)

        utxos = nodes[3].getaddressutxos({"addresses": [addr256]})
        assert_equal(len(utxos), 1)

        mempool = nodes[3].getaddressmempool({"addresses": [addr256]})
        assert_equal(len(mempool), 0)

        # Bitcoin segwit addresses
        self.log.info("Testing Bitcoin segwit addresses...")

        addr_sw_bech32 = nodes[2].getnewaddress('segwit script', False, False, False, 'bech32')

        secret_key = generate_privkey()
        internal_key = compute_xonly_pubkey(secret_key)[0]

        tri = taproot_construct(internal_key)
        scriptPubKey = tri.scriptPubKey

        addr_sw_tr = encode_segwit_address("rtpw", 1, scriptPubKey[2:])
        addrs = (addr_sw_bech32, addr_sw_tr)

        for a in addrs:
            nodes[0].sendtoaddress(a, 1.0)
        self.sync_all()
        mempool_txns = {}
        for a in addrs:
            mempool = nodes[1].getaddressmempool({'addresses': [a]})
            assert (len(mempool) == 1)
            assert (mempool[0]['address'] == a)
            mempool_txns[a] = mempool

        utxo = mempool_txns[addr_sw_bech32]
        inputs = [{'txid': utxo[0]['txid'], 'vout': utxo[0]['index']}]
        outputs = {nodes[0].getnewaddress(): 0.99}
        tx = nodes[2].createrawtransaction(inputs, outputs)
        tx = nodes[2].signrawtransactionwithwallet(tx)['hex']
        nodes[2].sendrawtransaction(tx)

        mempool_deltas = nodes[2].getaddressmempool({'addresses': [addr_sw_bech32]})
        assert_equal(len(mempool_deltas), 2)

        utxo = mempool_txns[addr_sw_tr]
        inputs = [{'txid': utxo[0]['txid'], 'vout': utxo[0]['index']}]
        outputs = {nodes[0].getnewaddress(): 0.99}
        tx = nodes[2].createrawtransaction(inputs, outputs)
        options = {'taproot': {tri.output_pubkey.hex(): {'internal_key_index': 0}}}
        tx = nodes[2].signrawtransactionwithkey(tx, [bytes_to_wif(secret_key)], [], 'DEFAULT', options)
        nodes[2].sendrawtransaction(tx['hex'])

        mempool_deltas = nodes[2].getaddressmempool({'addresses': [addr_sw_tr]})
        assert_equal(len(mempool_deltas), 2)

        pk0 = nodes[2].getaddressinfo(nodes[2].getnewaddress())['pubkey']
        pk1 = nodes[2].getaddressinfo(nodes[2].getnewaddress())['pubkey']

        ms_btcnative = nodes[2].addmultisigaddress_part(1, [pk0, pk1], 'ms_btcnative', False, False, 'bech32')
        nodes[0].sendtoaddress(ms_btcnative['address'], 1.0)

        self.sync_all()
        mempool_sw_ms = nodes[1].getaddressmempool({'addresses': [ms_btcnative['address']]})
        assert (len(mempool_sw_ms) == 1)
        assert (mempool_sw_ms[0]['address'] == ms_btcnative['address'])

        inputs = [{'txid': mempool_sw_ms[0]['txid'], 'vout': mempool_sw_ms[0]['index']}]
        outputs = {nodes[0].getnewaddress(): 0.99}
        tx = nodes[2].createrawtransaction(inputs, outputs)
        tx = nodes[2].signrawtransactionwithwallet(tx)['hex']
        nodes[2].sendrawtransaction(tx)

        mempool_deltas = nodes[2].getaddressmempool({'addresses': [addr_sw_bech32]})
        assert_equal(len(mempool_deltas), 2)


if __name__ == '__main__':
    AddressIndexTest().main()
