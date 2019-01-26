#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

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
import time
import traceback
import sys
import json
import subprocess
from avocado import Test, main

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')

import ServerUtils
import WriteHostFile
from daos_api import DaosContext, DaosPool, DaosServer, DaosApiError

class RebuildNoCap(Test):

    """
    Test Class Description:
    This class contains tests for pool rebuild.

    :avocado: tags=pool,rebuild,nocap
    """

    build_paths = []
    server_group = ""
    CONTEXT = None
    POOL = None

    def setUp(self):
        """ setup for the test """

        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as f:
              build_paths = json.load(f)
        self.CONTEXT = DaosContext(build_paths['PREFIX'] + '/lib/')
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")

        # generate a hostfile
        self.hostlist = self.params.get("test_machines",'/run/hosts/')

        self.hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.workdir)

        # fire up the DAOS servers
        self.server_group = self.params.get("name", '/run/server/', 'daos_server')
        ServerUtils.runServer(self, self.server_group)
        time.sleep(3)

        # create a pool to test with
        createmode = self.params.get("mode",'/run/pool/createmode/')
        createuid  = self.params.get("uid",'/run/pool/createuid/')
        creategid  = self.params.get("gid",'/run/pool/creategid/')
        createsetid = self.params.get("setname",'/run/pool/createset/')
        createsize  = self.params.get("size",'/run/pool/createsize/')
        self.POOL = DaosPool(self.CONTEXT)
        self.POOL.create(createmode, createuid, creategid, createsize,
                         createsetid)
        uuid = self.POOL.get_uuid_str()

        time.sleep(2)

        # stuff some bogus data into the pool
        how_many_bytes = long(self.params.get("datasize",
                '/run/testparams/datatowrite/'))
        exepath = build_paths['PREFIX'] +\
                 "/../src/tests/ftest/util/WriteSomeData.py"
        cmd = "export DAOS_POOL={0}; export DAOS_SVCL=1; mpirun"\
              " --np 1 --host {1} {2} {3} testfile".format(
                 uuid, self.hostlist[0], exepath, how_many_bytes)
        subprocess.call(cmd, shell=True)

    def tearDown(self):
        """ cleanup after the test """

        try:
            if self.POOL:
                self.POOL.destroy(1)
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)


    def test_rebuild_no_capacity(self):
        """
        :avocado: tags=pool,rebuild,nocap
        """
        try:
            print "\nsetup complete, starting test\n"

            # create a server object that references on of our pool target hosts
            # and then kill it
            svr_to_kill = int(self.params.get("rank_to_kill",
                                              '/run/testparams/ranks/'))
            sh = DaosServer(self.CONTEXT, bytes(self.server_group), svr_to_kill)

            time.sleep(1)
            sh.kill(1)

            # exclude the target from the dead server
            self.POOL.exclude([svr_to_kill])

            # exclude should trigger rebuild, check
            self.POOL.connect(1 << 1)
            status = self.POOL.pool_query()
            if not status.pi_ntargets == len(self.hostlist):
                self.fail("target count wrong.\n")
            if not status.pi_ndisabled == 1:
                self.fail("disabled target count wrong.\n")

            # the pool should be too full to start a rebuild so
            # expecting an error
            # not sure yet specifically what error
            if status.pi_rebuild_st.rs_errno == 0:
                self.fail("expecting rebuild to fail but it didn't.\n")

        except DaosApiError as e:
                print(e)
                print(traceback.format_exc())
                self.fail("Expecting to pass but test has failed.\n")

if __name__ == "__main__":
    main()
