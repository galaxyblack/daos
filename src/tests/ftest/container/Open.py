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
import uuid

from apricot       import Test
from avocado       import main
from avocado.utils import process

import ServerUtils
import WriteHostFile

from daos_cref import RankList
from daos_api import DaosContext, DaosPool, DaosContainer, DaosApiError

class OpenContainerTest(Test):
    """
    Tests DAOS container bad create (non existing pool handle, bad uuid)
    and close.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        super(OpenContainerTest, self).__init__(*args, **kwargs)
        self.POOL1 = None
        self.POOL2 = None
        self.CONTAINER1 = None
        self.CONTAINER2 = None

    def setUp(self):
        self.POOL1 = None
        self.POOL2 = None
        self.CONTAINER1 = None
        self.CONTAINER2 = None

        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as f:
            build_paths = json.load(f)
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        self.tmp = build_paths['PREFIX'] + '/tmp'

        self.server_group = self.params.get("server_group",'/server/',
                                           'daos_server')

        # setup the DAOS python API
        self.Context = DaosContext(build_paths['PREFIX'] + '/lib/')

        self.hostlist = self.params.get("test_machines",'/run/hosts/*')
        self.hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.tmp)

        # common parameters used in pool create
        self.createmode = self.params.get("mode",'/run/createtests/createmode/')
        self.createsetid = self.params.get("setname",'/run/createtests/createset/')
        self.createsize  = self.params.get("size",'/run/createtests/createsize/')

        # POOL 1 UID GID
        self.createuid1  = self.params.get("uid",'/run/createtests/createuid1/')
        self.creategid1  = self.params.get("gid",'/run/createtests/creategid1/')

        # POOL 2 UID GID
        self.createuid2  = self.params.get("uid",'/run/createtests/createuid2/')
        self.creategid2  = self.params.get("gid",'/run/createtests/creategid2/')

        ServerUtils.runServer(self.hostfile, self.server_group, self.basepath)

    def tearDown(self):
        try:
            if self.CONTAINER1 is not None:
                self.CONTAINER1.destroy()
            if self.CONTAINER2 is not None:
                self.CONTAINER2.destroy()
            if self.POOL1 is not None and self.POOL1.attached:
                self.POOL1.destroy(1)
            if self.POOL2 is not None and self.POOL2.attached:
                self.POOL2.destroy(1)
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)

    def test_container_open(self):
        """
        Test basic container bad create.

        :avocado: tags=container,containeropen
        """

        expected_for_param = []
        uuidlist  = self.params.get("uuid",'/run/createtests/uuids/*/')
        containerUUID = uuidlist[0]
        expected_for_param.append(uuidlist[1])

        pohlist = self.params.get("poh",'/run/createtests/handles/*/')
        poh = pohlist[0]
        expected_for_param.append(pohlist[1])

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        try:
            # create two pools and try to create containers in these pools
            self.POOL1 = DaosPool(self.Context)
            self.POOL1.create(self.createmode, self.createuid1, self.creategid1,
                              self.createsize, self.createsetid, None)

            self.POOL2 = DaosPool(self.Context)
            self.POOL2.create(self.createmode, self.createuid2, self.creategid2,
                              self.createsize, None, None)

            # Connect to the pools
            self.POOL1.connect(1 << 1)
            self.POOL2.connect(1 << 1)

            # defines pool handle for container open
            if pohlist[0] == 'POOL1':
                poh = self.POOL1.handle
            else:
                poh = self.POOL2.handle

            # Create a container in POOL1
            self.CONTAINER1 = DaosContainer(self.Context)
            self.CONTAINER1.create(self.POOL1.handle)

            # defines test UUID for container open
            if uuidlist[0] == 'POOL1':
                struuid = self.CONTAINER1.get_uuid_str()
                containerUUID = uuid.UUID(struuid)
            else:
                if uuidlist[0] == 'MFUUID':
                    containerUUID = "misformed-uuid-0000"
                else:
                    containerUUID = uuid.uuid4() # random uuid

            # tries to open the container1
            # open should be ok only if poh = POOL1.handle && containerUUID = CONTAINER1.uuid
            self.CONTAINER1.open(poh, containerUUID)

            # wait a few seconds and then destroy containers
            time.sleep(5)
            self.CONTAINER1.close()
            self.CONTAINER1.destroy()
            self.CONTAINER1 = None

            # cleanup the pools
            self.POOL1.disconnect()
            self.POOL1.destroy(1)
            self.POOL1 = None
            self.POOL2.disconnect()
            self.POOL2.destroy(1)
            self.POOL2 = None

            if expected_result in ['FAIL']:
                    self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as e:
            print(e)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
        finally:
            if self.hostfile is not None:
                os.remove(self.hostfile)

if __name__ == "__main__":
    main()
