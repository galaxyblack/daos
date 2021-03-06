#!/usr/bin/python
'''
    (C) Copyright 2018 Intel Corporation.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
    The Government's rights to use, modify, reproduce, release, perform, display,
    or disclose this software are subject to the terms of the Apache License as
    provided in Contract No. B609815.
    Any reproduction of computer software, computer software documentation, or
    portions thereof marked with this legend must also reproduce the markings.
    '''

import os
import sys
import json
from avocado       import Test

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import ServerUtils
import WriteHostFile
import IorUtils
from daos_api import DaosContext, DaosPool, DaosApiError

class MultipleClients(Test):
    """
    Test class Description: Runs IOR with multiple clients.

    """
    def setUp(self):
        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as f:
            build_paths = json.load(f)
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")

        self.server_group = self.params.get("server_group", '/server/','daos_server')
        self.daosctl = self.basepath + '/install/bin/daosctl'

        # setup the DAOS python API
        self.Context = DaosContext(build_paths['PREFIX'] + '/lib/')
        self.pool = None

        self.hostlist_servers = self.params.get("test_servers", '/run/hosts/test_machines/*')
        self.hostfile_servers = WriteHostFile.WriteHostFile(self.hostlist_servers, self.workdir)
        print("Host file servers is: {}".format(self.hostfile_servers))

        self.hostlist_clients = self.params.get("clients", '/run/hosts/test_machines/test_clients/*')
        self.hostfile_clients = WriteHostFile.WriteHostFile(self.hostlist_clients, self.workdir)
        print("Host file clientsis: {}".format(self.hostfile_clients))

        ServerUtils.runServer(self.hostfile_servers, self.server_group, self.basepath)

        if int(str(self.name).split("-")[0]) == 1:
            IorUtils.build_ior(self.basepath)

    def tearDown(self):
        try:
            if self.hostfile_clients is not None:
                os.remove(self.hostfile_clients)
            if self.hostfile_servers is not None:
                os.remove(self.hostfile_servers)
            if self.pool is not None and self.pool.attached:
                self.pool.destroy(1)
        finally:
            ServerUtils.stopServer(hosts=self.hostlist_servers)

    def test_multipleclients(self):
        """
        Test ID: DAOS-1263
        Test Description: Test IOR with 16 and 32 clients config.
        Use Cases: Different combinations of 16/32 Clients, 8b/1k/4k
                   record size, 1m/8m stripesize and 16 async io.
        :avocado: tags=ior,twoservers,multipleclients
        """

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/pool/createmode/*/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/pool/createset/')
        createsize = self.params.get("size", '/run/pool/createsize/')
        createsvc = self.params.get("svcn", '/run/pool/createsvc/')
        iteration = self.params.get("iter", '/run/ior/iteration/')
        slots = self.params.get("slots", '/run/ior/clientslots/*')
        ior_flags = self.params.get("F", '/run/ior/iorflags/')
        transfer_size = self.params.get("t", '/run/ior/transfersize/')
        record_size = self.params.get("r", '/run/ior/recordsize/*')
        stripe_size = self.params.get("s", '/run/ior/stripesize/*')
        stripe_count = self.params.get("c", '/run/ior/stripecount/')
        async_io = self.params.get("a", '/run/ior/asyncio/')
        object_class = self.params.get("o", '/run/ior/objectclass/')

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.Context)
            self.pool.create(createmode, createuid, creategid,
                    createsize, createsetid, None, None, createsvc)


            with open(self.hostfile_clients) as f:
                newText=f.read().replace('slots=1', 'slots={0}').format(slots)

            with open(self.hostfile_clients, "w") as f:
                f.write(newText)

            pool_uuid = self.pool.get_uuid_str()
            list = []
            svc_list = ""
            for i in range(createsvc):
                list.append(int(self.pool.svc.rl_ranks[i]))
                svc_list += str(list[i]) + ":"
            svc_list = svc_list[:-1]

            if slots == 8:
                block_size = '3g'
            elif slots == 16:
                block_size = '1536m'

            if (stripe_size == '8m'):
                transfer_size = stripe_size

            IorUtils.run_ior(self.hostfile_clients, ior_flags, iteration, block_size, transfer_size,
                             pool_uuid, svc_list, record_size, stripe_size, stripe_count,
                             async_io, object_class, self.basepath, slots)

        except (DaosApiError, IorUtils.IorFailed) as e:
            self.fail("<MultipleClients Test run Failed>\n {}".format(e))
