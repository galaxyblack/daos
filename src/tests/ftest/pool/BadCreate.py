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
from avocado import Test, main

sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./util')
sys.path.append('./../../utils/py')

import ServerUtils
import WriteHostFile
from daos_api import DaosContext, DaosPool, DaosApiError

class BadCreateTest(Test):
    """
    Tests pool create API by passing NULL and otherwise inappropriate
    parameters.  This can't be done with daosctl, need to use the python API.

    :avocado: tags=pool,poolcreate,badparam,badcreate
    """

    # super wasteful since its doing this for every variation
    def setUp(self):

        self.hostlist = None

        # get paths from the build_vars generated by build
        with open(os.path.join(os.path.dirname(os.path.realpath(__file__)),
                               "../../../../.build_vars.json")) as f:
            build_paths = json.load(f)
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")

        self.hostlist = self.params.get("test_machines", '/run/hosts/')
        self.hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.workdir)

        server_group = self.params.get("name",
                                       '/server/',
                                       'daos_server')

        ServerUtils.runServer(self, server_group)

        time.sleep(2)

    def tearDown(self):
        ServerUtils.stopServer(hosts=self.hostlist)

    def test_create(self):
        """
        Pass bad parameters to pool create.

        :avocado: tags=pool,poolcreate,badparam,badcreate
        """
        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test

        pool = None
        expected_for_param = []

        modelist = self.params.get("mode", '/run/createtests/modes/*')
        mode = modelist[0]
        expected_for_param.append(modelist[1])

        uidlist = self.params.get("uid", '/run/createtests/uids/*')
        uid = uidlist[0]
        expected_for_param.append(uidlist[1])

        gidlist = self.params.get("gid", '/run/createtests/gids/*')
        gid = gidlist[0]
        expected_for_param.append(gidlist[1])

        setidlist = self.params.get("setname", '/run/createtests/setnames/*')
        if setidlist[0] == 'NULLPTR':
            group = None
            self.cancel("skipping this test until DAOS-1991 is fixed")
        else:
            group = setidlist[0]
        expected_for_param.append(setidlist[1])

        targetlist = self.params.get("rankptr", '/run/createtests/target/*')
        if targetlist[0] == 'NULL':
            targetptr = None
        else:
            targetptr = [0]
        expected_for_param.append(targetlist[1])

        # not ready for this yet
        #devicelist = self.params.get("devptr", '/run/createtests/device/*')
        #if devicelist[0] == 'NULL':
        #    devptr = None
        #else:
        #    devptr = devicelist[0]
        #expected_for_param.append(devicelist[1])

        sizelist = self.params.get("size", '/run/createtests/psize/*')
        size = sizelist[0]
        expected_for_param.append(sizelist[1])

        # parameter not presently supported
        #svclist = self.params.get("rankptr", '/run/createtests/svc/*')
        #if svclist[0] == 'NULL':
        #    svc = None
        #else:
        #    svc = None
        #expected_for_param.append(devicelist[1])

        # if any parameter is FAIL then the test should FAIL, in this test
        # virtually everyone should FAIL since we are testing bad parameters
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        try:
            # setup the DAOS python API
            with open('../../../.build_vars.json') as f:
                data = json.load(f)
            context = DaosContext(data['PREFIX'] + '/lib/')

            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(context)
            pool.create(mode, uid, gid, size, group, targetptr)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as e:
            print(e)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
        finally:
            if pool is not None and pool.attached:
                pool.destroy(1)

if __name__ == "__main__":
    main()
